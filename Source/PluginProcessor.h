#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/ResonanceSuppressor.h"

namespace ParamIDs
{
    inline constexpr const char* depth       = "depth";
    inline constexpr const char* sharpness   = "sharpness";
    inline constexpr const char* selectivity = "selectivity";
    inline constexpr const char* attack      = "attack";
    inline constexpr const char* release     = "release";
    inline constexpr const char* focusLow    = "focusLow";
    inline constexpr const char* focusHigh   = "focusHigh";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* output      = "output";
    inline constexpr const char* delta       = "delta";
    inline constexpr const char* link        = "link";
}

class VelourAudioProcessor : public juce::AudioProcessor
{
public:
    VelourAudioProcessor();
    ~VelourAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    velour::ResonanceSuppressor suppressor;

private:
    velour::SuppressorParams readParams() const;
    void runSuppressor (juce::AudioBuffer<float>& buffer, const velour::SuppressorParams& p);

    std::atomic<float>* depth = nullptr;
    std::atomic<float>* sharpness = nullptr;
    std::atomic<float>* selectivity = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* focusLow = nullptr;
    std::atomic<float>* focusHigh = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* output = nullptr;
    std::atomic<float>* delta = nullptr;
    std::atomic<float>* link = nullptr;

    int preparedChannels = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelourAudioProcessor)
};
