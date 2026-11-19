#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace v44;

namespace
{
void maxStore (std::atomic<float>& a, float v) noexcept
{
    float c = a.load();
    while (v > c && ! a.compare_exchange_weak (c, v)) {}
}

struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Default", {} },
        { "Rap Lead",    { { "gate", -62.0f }, { "comp", 0.55f }, { "color", 0.6f }, { "deess", 0.4f }, { "eq_low", 2.0f },
                           { "eq_high", 2.0f }, { "eq_air", 4.0f }, { "rev_mix", 0.08f }, { "dly_mix", 0.12f }, { "sfx_amt", 0.3f } } },
        { "Pop Brillante", { { "comp", 0.4f }, { "deess", 0.5f }, { "eq_mid", -1.0f }, { "eq_high", 3.0f }, { "eq_air", 7.0f },
                             { "tone_mode", 2.0f }, { "rev_mix", 0.2f }, { "rev_time", 1.8f }, { "space_mode", 1.0f } } },
        { "Balada Calida", { { "dyn_mode", 0.0f }, { "comp", 0.35f }, { "tone_mode", 1.0f }, { "eq_low", 2.0f }, { "eq_high", -1.0f },
                             { "rev_mix", 0.28f }, { "rev_time", 3.2f }, { "space_mode", 3.0f }, { "dly_mix", 0.06f } } },
        { "Radio Vox",   { { "dyn_mode", 1.0f }, { "comp", 0.6f }, { "sfx_mode", 3.0f }, { "sfx_amt", 0.8f }, { "sfx_route", 0.0f },
                           { "rev_mix", 0.1f }, { "space_mode", 2.0f } } }
    };
    return list;
}
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout Vocal44Processor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;

    auto addB = [&] (const char* id, const char* name, bool def)
    { v.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto addF = [&] (const char* id, const char* name, float lo, float hi, float def, float step = 0.01f)
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, step), def)); };
    auto addC = [&] (const char* id, const char* name, StringArray choices, int def)
    { v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def)); };

    addB ("bypass", "Bypass", false);
    addF ("in_gain", "Input", -24.0f, 24.0f, 0.0f, 0.1f);
    addF ("out_gain", "Output", -24.0f, 24.0f, 0.0f, 0.1f);

    // Dynamics
    addB ("dyn_on", "Dynamics On", true);
    addC ("dyn_mode", "Dynamics Mode", { "OPTO", "FET", "OPTO + FET" }, 2);
    addF ("dyn_gain", "Dynamics Gain", -12.0f, 12.0f, 0.0f, 0.1f);
    addF ("gate", "Gate", -90.0f, -10.0f, -90.0f, 0.5f);
    addF ("comp", "Comp", 0.0f, 1.0f, 0.35f);
    addF ("color", "Color", 0.0f, 1.0f, 0.5f);
    addF ("deess", "De-Ess", 0.0f, 1.0f, 0.3f);
    addF ("deess_f", "De-Ess Focus", 2000.0f, 12000.0f, 5200.0f, 10.0f);

    // Tone
    addB ("tone_on", "Tone On", true);
    addC ("tone_mode", "Tone Mode", { "MODERN", "WARM", "BRIGHT", "VINTAGE" }, 0);
    addF ("tone_gain", "Tone Gain", -12.0f, 12.0f, 0.0f, 0.1f);
    addF ("eq_low", "Low", -12.0f, 12.0f, 0.0f, 0.1f);
    addF ("eq_mid", "Mid", -12.0f, 12.0f, 0.0f, 0.1f);
    addF ("eq_high", "High", -12.0f, 12.0f, 0.0f, 0.1f);
    addF ("eq_air", "Air+", 0.0f, 12.0f, 0.0f, 0.1f);

    // Space
    addB ("space_on", "Space On", true);
    addC ("space_mode", "Space Mode", { "DARK HALL", "PLATE", "ROOM", "CATHEDRAL" }, 0);
    addF ("space_gain", "Space Gain", -12.0f, 12.0f, 0.0f, 0.1f);
    addF ("rev_mix", "Reverb", 0.0f, 1.0f, 0.15f);
    addF ("rev_time", "Reverb Time", 0.2f, 6.0f, 2.3f, 0.1f);
    addF ("dly_mix", "Delay", 0.0f, 1.0f, 0.10f);
    addC ("dly_rate", "Delay Rate", { "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/2" }, 2);
    addB ("dly_ping", "Ping-Pong", true);
    addF ("vrb_duck", "Reverb Duck", 0.0f, 1.0f, 0.0f);
    addF ("dly_duck", "Delay Duck", 0.0f, 1.0f, 0.0f);

    // SFX
    addB ("sfx_on", "SFX On", true);
    addC ("sfx_mode", "SFX Mode", { "PITCH WIDENER", "CHORUS", "DOUBLER", "LO-FI RADIO", "EXCITER" }, 0);
    addF ("sfx_gain", "SFX Gain", -12.0f, 12.0f, 0.0f, 0.1f);
    addF ("sfx_amt", "SFX", 0.0f, 1.0f, 0.25f);
    addC ("sfx_route", "SFX Routing", { "PRE SPACE", "POST SPACE" }, 1);

    return { v.begin(), v.end() };
}

//==============================================================================
Vocal44Processor::Vocal44Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    p = { get ("bypass"), get ("in_gain"), get ("out_gain"),
          get ("dyn_on"), get ("dyn_mode"), get ("dyn_gain"), get ("gate"), get ("comp"), get ("color"), get ("deess"), get ("deess_f"),
          get ("tone_on"), get ("tone_mode"), get ("tone_gain"), get ("eq_low"), get ("eq_mid"), get ("eq_high"), get ("eq_air"),
          get ("space_on"), get ("space_mode"), get ("space_gain"), get ("rev_mix"), get ("rev_time"), get ("dly_mix"), get ("dly_rate"),
          get ("dly_ping"), get ("vrb_duck"), get ("dly_duck"),
          get ("sfx_on"), get ("sfx_mode"), get ("sfx_gain"), get ("sfx_amt"), get ("sfx_route") };
}

bool Vocal44Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Vocal44Processor::prepareToPlay (double sr, int block)
{
    gate.prepare (sr);
    dynamics.prepare (sr);
    deesser.prepare (sr, block);
    tone.prepare (sr);
    space.prepare (sr, block);
    sfx.prepare (sr, block);
}

void Vocal44Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) return;

    if (p.bypass->load() > 0.5f) return;

    buffer.applyGain (toLin (p.inGain->load()));
    const float inPk = std::max (buffer.getMagnitude (0, 0, n), buffer.getMagnitude (1, 0, n));
    maxStore (meters.inPeak, inPk);
    maxStore (meters.gateIn, inPk);

    // ---- Gate
    const float gateDb = p.gate->load();
    if (gateDb > -89.5f) gate.process (buffer, gateDb);

    // ---- Dynamics
    if (p.dynOn->load() > 0.5f)
    {
        const float gr = dynamics.process (buffer, p.comp->load(), p.color->load(), (int) p.dynMode->load());
        const float ds = deesser.process (buffer, p.deess->load(), p.deessF->load());
        buffer.applyGain (toLin (p.dynGain->load()));
        maxStore (meters.gr, gr);
        maxStore (meters.ds, ds);
    }

    // ---- Tone
    if (p.toneOn->load() > 0.5f)
    {
        tone.process (buffer, (int) p.toneMode->load(), p.eqLow->load(), p.eqMid->load(), p.eqHigh->load(), p.eqAir->load());
        buffer.applyGain (toLin (p.toneGain->load()));
    }

    const bool sfxOn = p.sfxOn->load() > 0.5f;
    const bool sfxPre = p.sfxRoute->load() < 0.5f;
    auto runSfx = [&]
    {
        sfx.process (buffer, (int) p.sfxMode->load(), p.sfxAmt->load());
        buffer.applyGain (toLin (p.sfxGain->load()));
    };

    if (sfxOn && sfxPre) runSfx();

    // ---- Space
    if (p.spaceOn->load() > 0.5f)
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    bpm = *b;

        SpaceFx::Params sp { p.revMix->load(), p.revTime->load(), p.dlyMix->load(), (int) p.dlyRate->load(),
                             p.dlyPing->load() > 0.5f, p.vrbDuck->load(), p.dlyDuck->load(), (int) p.spaceMode->load(), bpm };
        SpaceFx::Meters sm;
        space.process (buffer, sp, sm);
        buffer.applyGain (toLin (p.spaceGain->load()));
        maxStore (meters.vrbIn, sm.vrbIn);   maxStore (meters.vrbDuck, sm.vrbDuck); maxStore (meters.vrbOut, sm.vrbOut);
        maxStore (meters.dlyIn, sm.dlyIn);   maxStore (meters.dlyDuck, sm.dlyDuck); maxStore (meters.dlyOut, sm.dlyOut);
    }

    if (sfxOn && ! sfxPre) runSfx();

    buffer.applyGain (toLin (p.outGain->load()));
    maxStore (meters.outPeak, std::max (buffer.getMagnitude (0, 0, n), buffer.getMagnitude (1, 0, n)));

    // analizador
    int pos = analyzerPos.load();
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (1);
    for (int i = 0; i < n; ++i)
        analyzer[(size_t) ((pos + i) & 4095)] = 0.5f * (l[i] + r[i]);
    analyzerPos.store ((pos + n) & 4095);
}

//==============================================================================
int Vocal44Processor::getNumPrograms() { return (int) presets().size(); }

const juce::String Vocal44Processor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) presets().size())) return presets()[(size_t) index].name;
    return {};
}

void Vocal44Processor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size())) return;
    currentProgram = index;

    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = rp->paramID;
            if (id == "bypass" || id == "in_gain" || id == "out_gain") continue;
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }

    for (auto& kv : presets()[(size_t) index].values)
        if (auto* rp = apvts.getParameter (kv.first))
            rp->setValueNotifyingHost (rp->convertTo0to1 (kv.second));
}

void Vocal44Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void Vocal44Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* Vocal44Processor::createEditor() { return new Vocal44Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Vocal44Processor(); }
