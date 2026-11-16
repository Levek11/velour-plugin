#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
class VelourLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VelourLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

//==============================================================================
// Real-time spectrum with the reduction curve hanging from the top.
class SpectrumDisplay : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumDisplay (VelourAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    float xForFreq (float hz) const;
    float freqForX (float x) const;

    VelourAudioProcessor& processor;
    std::vector<float> spectrum, reduction;
    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    static constexpr float minDb = -110.0f, maxDb = -10.0f, maxRedDb = 20.0f;
};

//==============================================================================
class VelourAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit VelourAudioProcessorEditor (VelourAudioProcessor&);
    ~VelourAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (Knob& k, const char* paramId, const juce::String& name);

    VelourAudioProcessor& processor;
    VelourLookAndFeel lnf;
    SpectrumDisplay display;

    Knob depth, sharpness, selectivity, attack, release, focusLow, focusHigh, mix, output;
    juce::ToggleButton deltaButton { "Delta" }, linkButton { "Stereo Link" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttach, linkAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelourAudioProcessorEditor)
};
