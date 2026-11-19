#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "DSP.h"

class Vocal44Processor : public juce::AudioProcessor
{
public:
    Vocal44Processor();
    ~Vocal44Processor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "44 Vocals"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    // Medidores: el procesador acumula el pico maximo, la interfaz lo lee y lo reinicia (exchange(0))
    struct Meters
    {
        std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, gateIn { 0.0f },
                           gr { 0.0f }, ds { 0.0f },
                           vrbIn { 0.0f }, vrbDuck { 0.0f }, vrbOut { 0.0f },
                           dlyIn { 0.0f }, dlyDuck { 0.0f }, dlyOut { 0.0f };
    } meters;

    // Buffer circular para el analizador de espectro
    std::array<float, 4096> analyzer {};
    std::atomic<int> analyzerPos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Ptrs
    {
        std::atomic<float> *bypass, *inGain, *outGain,
            *dynOn, *dynMode, *dynGain, *gate, *comp, *color, *deess, *deessF,
            *toneOn, *toneMode, *toneGain, *eqLow, *eqMid, *eqHigh, *eqAir,
            *spaceOn, *spaceMode, *spaceGain, *revMix, *revTime, *dlyMix, *dlyRate, *dlyPing, *vrbDuck, *dlyDuck,
            *sfxOn, *sfxMode, *sfxGain, *sfxAmt, *sfxRoute;
    } p {};

    v44::Gate gate;
    v44::Dynamics dynamics;
    v44::DeEsser deesser;
    v44::ToneEq tone;
    v44::SpaceFx space;
    v44::Sfx sfx;

    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Vocal44Processor)
};
