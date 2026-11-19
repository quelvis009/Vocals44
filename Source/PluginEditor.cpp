#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
float norm01 (std::atomic<float>& a, float minDb)
{
    return juce::jlimit (0.0f, 1.0f, (v44::toDb (a.exchange (0.0f)) - minDb) / (-minDb));
}
float normGr (std::atomic<float>& a, float maxDb)
{
    return juce::jlimit (0.0f, 1.0f, a.exchange (0.0f) / maxDb);
}
float normLin (std::atomic<float>& a) { return juce::jlimit (0.0f, 1.0f, a.exchange (0.0f)); }
}

//==============================================================================
// Analizador + curva EQ
class EqDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EqDisplay (Vocal44Processor& p) : proc (p) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a0a0b));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto area = r.reduced (8.0f, 10.0f);
        auto xOf = [&] (float f) { return area.getX() + area.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
        auto yOf = [&] (float db) { return area.getCentreY() - db / 15.0f * area.getHeight() * 0.5f; };

        // rejilla
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            g.setColour (col::line);
            g.drawVerticalLine ((int) xOf (f), area.getY(), area.getBottom());
        }
        for (float db : { -12.0f, 0.0f, 12.0f })
        {
            g.setColour (db == 0.0f ? col::dim.withAlpha (0.8f) : col::line);
            g.drawHorizontalLine ((int) yOf (db), area.getX(), area.getRight());
            g.setColour (col::dim);
            g.drawText (juce::String ((int) db) + " dB", (int) area.getX() + 2, (int) yOf (db) - 11, 40, 10, juce::Justification::left);
        }

        // espectro
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
        juce::Path sp;
        bool started = false;
        for (int k = 2; k < fftSize / 2; ++k)
        {
            const float f = (float) (k * sr / fftSize);
            if (f < 20.0f || f > 20000.0f) continue;
            const float x = xOf (f);
            const float y = area.getBottom() - juce::jlimit (0.0f, 1.0f, (spec[(size_t) k] + 90.0f) / 80.0f) * area.getHeight() * 0.85f;
            if (! started) { sp.startNewSubPath (x, area.getBottom()); sp.lineTo (x, y); started = true; }
            else sp.lineTo (x, y);
        }
        if (started)
        {
            sp.lineTo (area.getRight(), area.getBottom());
            sp.closeSubPath();
            g.setColour (col::mustard.withAlpha (0.18f));
            g.fillPath (sp);
        }

        // curva EQ
        auto& a = proc.apvts;
        const int mode = (int) a.getRawParameterValue ("tone_mode")->load();
        const auto co = v44::makeEqCoeffs (sr, mode, a.getRawParameterValue ("eq_low")->load(), a.getRawParameterValue ("eq_mid")->load(),
                                           a.getRawParameterValue ("eq_high")->load(), a.getRawParameterValue ("eq_air")->load());
        auto respDb = [&] (float f)
        {
            double m = 1.0;
            for (auto& c : co) m *= c->getMagnitudeForFrequency ((double) f, sr);
            return juce::Decibels::gainToDecibels ((float) m);
        };

        juce::Path curve;
        for (int i = 0; i <= 160; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 160.0f);
            const float x = xOf (f), y = yOf (juce::jlimit (-15.0f, 15.0f, respDb (f)));
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (col::mustard.withAlpha (0.3f));
        g.strokePath (curve, juce::PathStrokeType (5.0f));
        g.setColour (col::mustardHi);
        g.strokePath (curve, juce::PathStrokeType (2.0f));

        const auto& fr = v44::eqMode (mode);
        for (float f : { fr.low, fr.mid, fr.high, fr.air })
        {
            g.setColour (col::mustardHi);
            g.fillEllipse (xOf (f) - 3.5f, yOf (juce::jlimit (-15.0f, 15.0f, respDb (f))) - 3.5f, 7.0f, 7.0f);
        }

        g.setColour (col::mustardLo);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText ("ANALYZER", (int) area.getX() + 2, (int) area.getBottom() - 11, 60, 10, juce::Justification::left);
        g.setColour (col::dim);
        for (float f : { 180.0f, 1000.0f, 5000.0f })
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "kHz" : juce::String ((int) f) + "Hz",
                        (int) xOf (f) - 20, (int) area.getBottom() - 11, 40, 10, juce::Justification::centred);
    }

private:
    static constexpr int fftSize = 2048;

    void timerCallback() override
    {
        const int pos = proc.analyzerPos.load();
        for (int i = 0; i < fftSize; ++i)
            data[(size_t) i] = proc.analyzer[(size_t) ((pos - fftSize + i) & 4095)];
        std::fill (data.begin() + fftSize, data.end(), 0.0f);
        window.multiplyWithWindowingTable (data.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (data.data());

        for (int k = 0; k < fftSize / 2; ++k)
        {
            const float db = v44::toDb (data[(size_t) k] / (fftSize * 0.25f));
            spec[(size_t) k] = std::max (db, spec[(size_t) k] - 1.5f);
        }
        repaint();
    }

    Vocal44Processor& proc;
    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * fftSize> data {};
    std::array<float, fftSize / 2> spec {};
};

//==============================================================================
// Base de modulo: encendido, titulo, icono, flechas de modo, ganancia
class ModulePanel : public juce::Component, private juce::Timer
{
public:
    ModulePanel (Vocal44Processor& p, juce::String title_, juce::Colour accent_, int icon,
                 const juce::String& onId, const juce::String& modeId_, const juce::String& gainId)
        : proc (p), title (std::move (title_)), accent (accent_), iconType (icon), modeId (modeId_),
          gainKnob (p.apvts, gainId, "", Fmt::Db, true)
    {
        power.accent = accent;
        power.setClickingTogglesState (true);
        addAndMakeVisible (power);
        powerAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, onId, power);

        left.setButtonText ("<");
        right.setButtonText (">");
        for (auto* b : { &left, &right })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            b->setColour (juce::TextButton::textColourOffId, accent);
            addAndMakeVisible (b);
        }
        left.onClick  = [this] { step (-1); };
        right.onClick = [this] { step (1); };

        modeLabel.setJustificationType (juce::Justification::centred);
        modeLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        modeLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        modeLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (modeLabel);

        gainKnob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        addAndMakeVisible (gainKnob);

        startTimerHz (15);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff202022), 0.0f, 0.0f, col::panel, 0.0f, (float) headerH, false));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (col::panelDark);
        g.fillRect (0.0f, (float) headerH, r.getWidth(), r.getHeight() - (float) headerH);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.drawHorizontalLine (headerH, 0.0f, r.getWidth());

        g.setColour (power.getToggleState() ? accent : col::dim);
        g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        g.drawText (title, getLocalBounds().removeFromTop (44), juce::Justification::centred);

        drawIcon (g, juce::Rectangle<float> (getWidth() * 0.5f - 55.0f, 52.0f, 110.0f, 80.0f));

        // puntos de pagina
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (modeId)))
        {
            const int n = cp->choices.size(), cur = cp->getIndex();
            const float gap = 11.0f, x0 = getWidth() * 0.5f - gap * (float) (n - 1) * 0.5f - 40.0f;
            for (int i = 0; i < n; ++i)
            {
                g.setColour (i == cur ? accent : col::line);
                g.fillEllipse (x0 + gap * (float) i - 2.5f, 170.0f, 5.0f, 5.0f);
            }
        }

        g.setColour (col::dim);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        for (auto& c : captions)
            g.drawText (c.first, c.second, juce::Justification::centred);
    }

    void resized() override
    {
        power.setBounds (12, 8, 30, 30);
        left.setBounds (8, 78, 28, 36);
        right.setBounds (getWidth() - 36, 78, 28, 36);
        modeLabel.setBounds (40, 138, getWidth() - 40 - 76, 26);
        gainKnob.setBounds (getWidth() - 68, 126, 60, 56);

        captions.clear();
        layoutBody (getLocalBounds().withTrimmedTop (headerH).reduced (10, 10));
    }

protected:
    static constexpr int headerH = 190;
    virtual void layoutBody (juce::Rectangle<int> body) = 0;
    void caption (const juce::String& t, juce::Rectangle<int> r) { captions.push_back ({ t, r }); }

    Vocal44Processor& proc;
    juce::Colour accent;

private:
    void step (int d)
    {
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (modeId)))
        {
            const int n = cp->choices.size(), i = (cp->getIndex() + d + n) % n;
            cp->beginChangeGesture();
            *cp = i;
            cp->endChangeGesture();
            repaint();
        }
    }

    void timerCallback() override
    {
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (modeId)))
        {
            auto t = cp->getCurrentChoiceName();
            if (modeLabel.getText() != t) { modeLabel.setText (t, juce::dontSendNotification); repaint(); }
        }
    }

    void drawIcon (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto c = r.getCentre();
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        juce::Path p, dots;
        constexpr float pi = juce::MathConstants<float>::pi;

        switch (iconType)
        {
            case 0: // bombilla
                p.addEllipse (c.x - s * 0.6f, c.y - s * 0.95f, s * 1.2f, s * 1.2f);
                for (float k : { 0.45f, 0.68f, 0.88f })
                { p.startNewSubPath (c.x - s * (0.38f - (k - 0.45f) * 0.5f), c.y + s * k); p.lineTo (c.x + s * (0.38f - (k - 0.45f) * 0.5f), c.y + s * k); }
                break;
            case 1: // curva EQ
                p.startNewSubPath (c.x - s * 1.2f, c.y + s * 0.2f);
                p.cubicTo (c.x - s * 0.7f, c.y + s * 0.2f, c.x - s * 0.7f, c.y - s * 0.55f, c.x - s * 0.1f, c.y - s * 0.5f);
                p.cubicTo (c.x + s * 0.3f, c.y - s * 0.45f, c.x + s * 0.4f, c.y + s * 0.4f, c.x + s * 1.2f, c.y + s * 0.3f);
                dots.addEllipse (c.x - s * 0.7f - 4.0f, c.y + s * 0.3f - 4.0f, 8.0f, 8.0f);
                dots.addEllipse (c.x + s * 0.0f - 4.0f, c.y - s * 0.5f - 4.0f, 8.0f, 8.0f);
                break;
            case 2: // domo
                p.addCentredArc (c.x, c.y + s * 0.6f, s * 1.1f, s * 1.3f, 0.0f, -pi * 0.5f, pi * 0.5f, true);
                p.closeSubPath();
                p.startNewSubPath (c.x, c.y - s * 0.7f); p.lineTo (c.x, c.y + s * 0.6f);
                p.startNewSubPath (c.x - s * 0.55f, c.y - s * 0.4f); p.lineTo (c.x - s * 0.55f, c.y + s * 0.6f);
                p.startNewSubPath (c.x + s * 0.55f, c.y - s * 0.4f); p.lineTo (c.x + s * 0.55f, c.y + s * 0.6f);
                break;
            default: // dos circulos
                p.addEllipse (c.x - s * 1.0f, c.y - s * 0.6f, s * 1.2f, s * 1.2f);
                p.addEllipse (c.x - s * 0.2f, c.y - s * 0.6f, s * 1.2f, s * 1.2f);
                break;
        }

        const auto col0 = power.getToggleState() ? accent : col::dim;
        g.setColour (col0.withAlpha (0.12f)); g.strokePath (p, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col0.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col0);                   g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillPath (dots);
    }

    juce::String title, modeId;
    int iconType;
    PowerButton power;
    juce::TextButton left, right;
    juce::Label modeLabel;
    Knob gainKnob;
    std::unique_ptr<APVTS::ButtonAttachment> powerAtt;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> captions;
};

//==============================================================================
class DynamicsPanel : public ModulePanel
{
public:
    explicit DynamicsPanel (Vocal44Processor& p)
        : ModulePanel (p, "DYNAMICS", col::mustard, 0, "dyn_on", "dyn_mode", "dyn_gain"),
          gateBar ([&p] { return norm01 (p.meters.gateIn, -90.0f); }),
          grBar ([&p] { return normGr (p.meters.gr, 18.0f); }, true),
          dsBar ([&p] { return normGr (p.meters.ds, 12.0f); }, true),
          comp (p.apvts, "comp", "COMP", Fmt::Percent),
          color (p.apvts, "color", "COLOR", Fmt::Percent, true),
          deess (p.apvts, "deess", "DE-ESS", Fmt::Percent),
          focus (p.apvts, "deess_f", "FOCUS", Fmt::Hz, true),
          gateFader (p.apvts, "gate", "", Fmt::Gate)
    {
        gateFader.slider.getProperties().set ("gate", true);
        gateFader.slider.setColour (juce::Slider::thumbColourId, col::mustardHi);
        for (auto* c : std::initializer_list<juce::Component*> { &gateBar, &grBar, &dsBar, &comp, &color, &deess, &focus, &gateFader })
            addAndMakeVisible (c);
    }

private:
    void layoutBody (juce::Rectangle<int> b) override
    {
        auto left = b.removeFromLeft (100);
        const int cw = 28, gap = 6;
        auto col1 = juce::Rectangle<int> (left.getX(), left.getY(), cw, left.getHeight());
        auto col2 = col1.translated (cw + gap, 0), col3 = col2.translated (cw + gap, 0);
        caption ("GATE", col1.removeFromTop (16)); col1.removeFromBottom (4);
        gateBar.setBounds (col1);
        gateFader.setBounds (col1.expanded (6, 8));
        caption ("GR", col2.removeFromTop (16));   grBar.setBounds (col2.withTrimmedBottom (4));
        caption ("DS", col3.removeFromTop (16));   dsBar.setBounds (col3.withTrimmedBottom (4));

        auto r = b.withTrimmedLeft (6);
        comp.setBounds (r.removeFromTop (105));
        color.setBounds (r.removeFromTop (82));
        deess.setBounds (r.removeFromTop (105));
        focus.setBounds (r.removeFromTop (82));
    }

    LevelBar gateBar, grBar, dsBar;
    Knob comp, color, deess, focus;
    Fader gateFader;
};

//==============================================================================
class TonePanel : public ModulePanel
{
public:
    explicit TonePanel (Vocal44Processor& p)
        : ModulePanel (p, "TONE", col::mustardHi, 1, "tone_on", "tone_mode", "tone_gain"),
          eq (p),
          low (p.apvts, "eq_low", "LOW", Fmt::Db),
          mid (p.apvts, "eq_mid", "MID", Fmt::Db),
          high (p.apvts, "eq_high", "HIGH", Fmt::Db),
          air (p.apvts, "eq_air", "AIR+", Fmt::Db)
    {
        for (auto* f : { &low, &mid, &high, &air })
        {
            f->slider.setColour (juce::Slider::thumbColourId, col::mustardHi);
            addAndMakeVisible (f);
        }
        addAndMakeVisible (eq);
    }

private:
    void layoutBody (juce::Rectangle<int> b) override
    {
        eq.setBounds (b.removeFromTop (190));
        b.removeFromTop (14);
        const int w = b.getWidth() / 4;
        for (auto* f : { &low, &mid, &high, &air })
            f->setBounds (b.removeFromLeft (w).withTrimmedBottom (4));
    }

    EqDisplay eq;
    Fader low, mid, high, air;
};

//==============================================================================
class SpacePanel : public ModulePanel
{
public:
    explicit SpacePanel (Vocal44Processor& p)
        : ModulePanel (p, "SPACE", col::mustard, 2, "space_on", "space_mode", "space_gain"),
          vIn ([&p] { return norm01 (p.meters.vrbIn, -60.0f); }),
          vDuck ([&p] { return normLin (p.meters.vrbDuck); }, true),
          vOut ([&p] { return norm01 (p.meters.vrbOut, -60.0f); }),
          dIn ([&p] { return norm01 (p.meters.dlyIn, -60.0f); }),
          dDuck ([&p] { return normLin (p.meters.dlyDuck); }, true),
          dOut ([&p] { return norm01 (p.meters.dlyOut, -60.0f); }),
          vrbDuck (p.apvts, "vrb_duck", "VRB DUCK", Fmt::Percent, true),
          dlyDuck (p.apvts, "dly_duck", "DLY DUCK", Fmt::Percent, true),
          reverb (p.apvts, "rev_mix", "REVERB", Fmt::Percent),
          delay (p.apvts, "dly_mix", "DELAY", Fmt::Percent),
          time (p.apvts, "rev_time", "TIME", Fmt::Sec, true)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &vIn, &vDuck, &vOut, &dIn, &dDuck, &dOut, &vrbDuck, &dlyDuck, &reverb, &delay, &time, &rate, &ping })
            addAndMakeVisible (c);

        rate.addItemList ({ "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/2" }, 1);
        rateAtt = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, "dly_rate", rate);
        ping.setButtonText ("PING");
        ping.setClickingTogglesState (true);
        pingAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "dly_ping", ping);
    }

private:
    void layoutBody (juce::Rectangle<int> b) override
    {
        const int colW = (b.getWidth() - 10) / 2;
        auto lc = b.removeFromLeft (colW);
        b.removeFromLeft (10);
        auto rc = b;

        auto column = [&] (juce::Rectangle<int> c, const juce::String& head, LevelBar& a, LevelBar& d, LevelBar& o,
                           Knob& duck, Knob& mix)
        {
            caption (head, c.removeFromTop (16));
            auto meters = c.removeFromTop (110);
            const int bw = 20, g2 = (meters.getWidth() - 3 * bw) / 4;
            auto m1 = juce::Rectangle<int> (meters.getX() + g2, meters.getY(), bw, meters.getHeight() - 14);
            auto m2 = m1.translated (bw + g2, 0), m3 = m2.translated (bw + g2, 0);
            a.setBounds (m1); d.setBounds (m2); o.setBounds (m3);
            caption ("IN", m1.withY (meters.getBottom() - 14).withHeight (14));
            caption ("DUCK", m2.withY (meters.getBottom() - 14).withHeight (14).expanded (6, 0));
            caption ("OUT", m3.withY (meters.getBottom() - 14).withHeight (14));
            c.removeFromTop (8);
            duck.setBounds (c.removeFromTop (78));
            mix.setBounds (c.removeFromTop (100));
            return c;
        };

        auto restL = column (lc, "REVERB", vIn, vDuck, vOut, vrbDuck, reverb);
        time.setBounds (restL.removeFromTop (70));

        auto restR = column (rc, "DELAY", dIn, dDuck, dOut, dlyDuck, delay);
        rate.setBounds (restR.removeFromTop (26).reduced (2, 0));
        restR.removeFromTop (6);
        ping.setBounds (restR.removeFromTop (24).reduced (2, 0));
    }

    LevelBar vIn, vDuck, vOut, dIn, dDuck, dOut;
    Knob vrbDuck, dlyDuck, reverb, delay, time;
    juce::ComboBox rate;
    juce::TextButton ping;
    std::unique_ptr<APVTS::ComboBoxAttachment> rateAtt;
    std::unique_ptr<APVTS::ButtonAttachment> pingAtt;
};

//==============================================================================
class SfxPanel : public ModulePanel
{
public:
    explicit SfxPanel (Vocal44Processor& p)
        : ModulePanel (p, "SFX", col::mustardHi, 3, "sfx_on", "sfx_mode", "sfx_gain"),
          sfx (p.apvts, "sfx_amt", "SFX", Fmt::Percent)
    {
        addAndMakeVisible (sfx);
        route.addItemList ({ "PRE SPACE", "POST SPACE" }, 1);
        routeAtt = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, "sfx_route", route);
        addAndMakeVisible (route);
    }

private:
    void layoutBody (juce::Rectangle<int> b) override
    {
        auto big = b.removeFromTop (250).reduced (10, 0);
        sfx.setBounds (big);
        b.removeFromTop (60);
        caption ("SFX ROUTING", b.removeFromTop (18));
        route.setBounds (b.removeFromTop (30).reduced (14, 0));
    }

    Knob sfx;
    juce::ComboBox route;
    std::unique_ptr<APVTS::ComboBoxAttachment> routeAtt;
};

//==============================================================================
class MainComponent : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (Vocal44Processor& p)
        : proc (p), dyn (p), tone (p), space (p), sfx (p),
          inBar ([&p] { return norm01 (p.meters.inPeak, -60.0f); }, false, true),
          outBar ([&p] { return norm01 (p.meters.outPeak, -60.0f); }, false, true)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &dyn, &tone, &space, &sfx, &inBar, &outBar })
            addAndMakeVisible (c);

        for (auto* s : { &inSlider, &outSlider })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, true, 70, 18);
            applyFormat (*s, Fmt::Db);
            addAndMakeVisible (s);
        }
        inAtt = std::make_unique<APVTS::SliderAttachment> (p.apvts, "in_gain", inSlider);
        outAtt = std::make_unique<APVTS::SliderAttachment> (p.apvts, "out_gain", outSlider);
        inSlider.setDoubleClickReturnValue (true, 0.0);
        outSlider.setDoubleClickReturnValue (true, 0.0);
        inSlider.updateText(); outSlider.updateText();

        for (auto* b : { &undoBtn, &redoBtn, &prevBtn, &nextBtn })
            addAndMakeVisible (b);
        undoBtn.setButtonText ("UNDO"); redoBtn.setButtonText ("REDO");
        prevBtn.setButtonText ("<");    nextBtn.setButtonText (">");
        undoBtn.onClick = [this] { proc.undoManager.undo(); };
        redoBtn.onClick = [this] { proc.undoManager.redo(); };
        prevBtn.onClick = [this] { stepPreset (-1); };
        nextBtn.onClick = [this] { stepPreset (1); };

        bypass.setButtonText ("BYPASS");
        bypass.setClickingTogglesState (true);
        bypass.setColour (juce::TextButton::buttonOnColourId, col::mustard);
        bypass.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible (bypass);
        bypassAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "bypass", bypass);

        presetLabel.setJustificationType (juce::Justification::centred);
        presetLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        presetLabel.setColour (juce::Label::textColourId, col::mustardHi);
        presetLabel.setColour (juce::Label::outlineColourId, col::line);
        addAndMakeVisible (presetLabel);

        startTimerHz (30);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);

        // logo animado "44 VOCALS"
        const float glow = 0.35f + 0.25f * std::sin (phase);
        auto lr = juce::Rectangle<float> (22.0f, 8.0f, 260.0f, 54.0f);
        g.setFont (juce::Font (juce::FontOptions (44.0f, juce::Font::bold)));
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (col::mustard.withAlpha (glow * 0.12f * (float) (5 - i)));
            g.drawText ("44", lr.translated ((float) -i * 0.5f, 0.0f).withWidth (80.0f), juce::Justification::centredLeft);
        }
        g.setColour (col::mustard);
        g.drawText ("44", lr.withWidth (80.0f), juce::Justification::centredLeft);

        g.setFont (juce::Font (juce::FontOptions (28.0f, juce::Font::bold)));
        g.setColour (juce::Colours::white);
        g.drawText ("VOCALS", lr.withTrimmedLeft (72.0f).withWidth (140.0f), juce::Justification::centredLeft);

        auto badge = juce::Rectangle<float> (lr.getX() + 214.0f, 24.0f, 44.0f, 22.0f);
        g.setColour (col::mustard);
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (juce::Colours::black);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText ("PRO", badge, juce::Justification::centred);

        // barra deslizante bajo el logo
        const float sweep = (std::sin (phase * 0.6f) * 0.5f + 0.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, lr.getX() + sweep * 200.0f, 0.0f,
                                                 col::mustard, lr.getX() + sweep * 200.0f + 60.0f, 0.0f, false));
        g.fillRect (lr.getX(), 64.0f, 260.0f, 2.0f);

        // barra inferior
        g.setColour (col::dim);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("INPUT", 24, 690, 50, 30, juce::Justification::centredLeft);
        g.drawText ("OUTPUT", 566, 690, 60, 30, juce::Justification::centredLeft);
    }

    void resized() override
    {
        undoBtn.setBounds (300, 20, 56, 30);
        redoBtn.setBounds (360, 20, 56, 30);
        prevBtn.setBounds (440, 16, 34, 38);
        presetLabel.setBounds (476, 16, 240, 38);
        nextBtn.setBounds (718, 16, 34, 38);
        bypass.setBounds (getWidth() - 130, 16, 106, 38);

        const int top = 76, h = 600, gap = 10, margin = 10;
        const int w = (getWidth() - 2 * margin - 3 * gap) / 4;
        int x = margin;
        for (auto* c : std::initializer_list<juce::Component*> { &dyn, &tone, &space, &sfx })
        { c->setBounds (x, top, w, h); x += w + gap; }

        inSlider.setBounds (76, 690, 250, 30);
        inBar.setBounds (336, 698, 200, 14);
        outSlider.setBounds (630, 690, 250, 30);
        outBar.setBounds (890, 698, 195, 14);
    }

private:
    void stepPreset (int d)
    {
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram ((proc.getCurrentProgram() + d + n) % n);
    }

    void timerCallback() override
    {
        phase += 0.07f;
        auto name = proc.getProgramName (proc.getCurrentProgram());
        if (presetLabel.getText() != name) presetLabel.setText (name, juce::dontSendNotification);
        repaint (0, 0, getWidth(), 76);
    }

    Vocal44Processor& proc;
    DynamicsPanel dyn;
    TonePanel tone;
    SpacePanel space;
    SfxPanel sfx;
    LevelBar inBar, outBar;
    juce::Slider inSlider, outSlider;
    juce::TextButton undoBtn, redoBtn, prevBtn, nextBtn, bypass;
    juce::Label presetLabel;
    std::unique_ptr<APVTS::SliderAttachment> inAtt, outAtt;
    std::unique_ptr<APVTS::ButtonAttachment> bypassAtt;
    float phase = 0.0f;
};

//==============================================================================
Vocal44Editor::Vocal44Editor (Vocal44Processor& p)
    : AudioProcessorEditor (&p)
{
    setLookAndFeel (&lnf);
    main = std::make_unique<MainComponent> (p);
    addAndMakeVisible (*main);
    main->setBounds (0, 0, baseW, baseH);

    setResizable (true, true);
    setResizeLimits (baseW / 2, baseH / 2, baseW * 3 / 2, baseH * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setSize (baseW, baseH);
}

Vocal44Editor::~Vocal44Editor()
{
    setLookAndFeel (nullptr);
}

void Vocal44Editor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void Vocal44Editor::resized()
{
    main->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseW));
}
