#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Logarithmic range, so Hz and ms knobs feel natural.
    juce::NormalisableRange<float> logRange (float min, float max)
    {
        return { min, max,
                 [] (float s, float e, float t) { return s * std::pow (e / s, t); },
                 [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
                 [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
    }

    juce::String formatHz (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }

    juce::String formatMs (float v, int)
    {
        return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms";
    }
}

VelourAudioProcessor::VelourAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VelourState", createParameterLayout())
{
    depth       = apvts.getRawParameterValue (ParamIDs::depth);
    sharpness   = apvts.getRawParameterValue (ParamIDs::sharpness);
    selectivity = apvts.getRawParameterValue (ParamIDs::selectivity);
    attack      = apvts.getRawParameterValue (ParamIDs::attack);
    release     = apvts.getRawParameterValue (ParamIDs::release);
    focusLow    = apvts.getRawParameterValue (ParamIDs::focusLow);
    focusHigh   = apvts.getRawParameterValue (ParamIDs::focusHigh);
    mix         = apvts.getRawParameterValue (ParamIDs::mix);
    output      = apvts.getRawParameterValue (ParamIDs::output);
    delta       = apvts.getRawParameterValue (ParamIDs::delta);
    link        = apvts.getRawParameterValue (ParamIDs::link);
}

juce::AudioProcessorValueTreeState::ParameterLayout VelourAudioProcessor::createParameterLayout()
{
    using namespace juce;
    using Attr = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto dB  = [] (float v, int) { return String (v, 1) + " dB"; };
    auto one = [] (float v, int) { return String (v, 1); };

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::depth, 1 }, "Depth",
        NormalisableRange<float> (0.0f, 20.0f, 0.01f), 6.0f, Attr().withStringFromValueFunction (dB)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::sharpness, 1 }, "Sharpness",
        NormalisableRange<float> (1.0f, 10.0f, 0.01f), 4.0f, Attr().withStringFromValueFunction (one)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::selectivity, 1 }, "Selectivity",
        NormalisableRange<float> (1.0f, 10.0f, 0.01f), 4.0f, Attr().withStringFromValueFunction (one)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::attack, 1 }, "Attack",
        logRange (0.5f, 50.0f), 5.0f, Attr().withStringFromValueFunction (formatMs)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::release, 1 }, "Release",
        logRange (5.0f, 500.0f), 60.0f, Attr().withStringFromValueFunction (formatMs)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::focusLow, 1 }, "Focus Low",
        logRange (20.0f, 5000.0f), 800.0f, Attr().withStringFromValueFunction (formatHz)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::focusHigh, 1 }, "Focus High",
        logRange (1000.0f, 20000.0f), 16000.0f, Attr().withStringFromValueFunction (formatHz)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)) + " %"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::output, 1 }, "Output",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, Attr().withStringFromValueFunction (dB)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::delta, 1 }, "Delta", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::link, 1 }, "Stereo Link", true));

    return { p.begin(), p.end() };
}

bool VelourAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void VelourAudioProcessor::prepareToPlay (double sampleRate, int)
{
    preparedChannels = juce::jmin (2, getTotalNumOutputChannels());
    suppressor.prepare (sampleRate, juce::jmax (1, preparedChannels));
    suppressor.setParams (readParams());
    setLatencySamples (suppressor.getLatencySamples());
}

velour::SuppressorParams VelourAudioProcessor::readParams() const
{
    velour::SuppressorParams p;
    p.depthDb     = depth->load();
    p.sharpness   = sharpness->load();
    p.selectivity = selectivity->load();
    p.attackMs    = attack->load();
    p.releaseMs   = release->load();
    p.focusLowHz  = focusLow->load();
    p.focusHighHz = juce::jmax (focusHigh->load(), focusLow->load() * 1.5f);
    p.mix         = mix->load() * 0.01f;
    p.outputDb    = output->load();
    p.delta       = delta->load() > 0.5f;
    p.stereoLink  = link->load() > 0.5f;
    return p;
}

void VelourAudioProcessor::runSuppressor (juce::AudioBuffer<float>& buffer, const velour::SuppressorParams& p)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int channels = juce::jmin (preparedChannels, buffer.getNumChannels());
    if (channels <= 0)
        return;

    suppressor.setParams (p);
    suppressor.process (buffer.getArrayOfWritePointers(), buffer.getNumSamples());
}

void VelourAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    runSuppressor (buffer, readParams());
}

// When Logic bypasses the plugin we keep running the engine with zero depth.
// That outputs the dry signal with the same latency, so bypassing never
// causes a timing jump, and the reduction fades in smoothly on re-enable.
void VelourAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto p = readParams();
    p.depthDb  = 0.0f;
    p.mix      = 1.0f;
    p.delta    = false;
    p.outputDb = 0.0f;
    runSuppressor (buffer, p);
}

void VelourAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VelourAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VelourAudioProcessor::createEditor()
{
    return new VelourAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VelourAudioProcessor();
}
