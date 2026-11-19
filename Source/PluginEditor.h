#pragma once
#include "PluginProcessor.h"
#include "Look.h"

class MainComponent;

class Vocal44Editor : public juce::AudioProcessorEditor
{
public:
    explicit Vocal44Editor (Vocal44Processor&);
    ~Vocal44Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MustardLNF lnf;
    std::unique_ptr<MainComponent> main;

    static constexpr int baseW = 1100, baseH = 730;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Vocal44Editor)
};
