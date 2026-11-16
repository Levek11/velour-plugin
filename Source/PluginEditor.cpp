#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour background  { 0xff16121c };
    const juce::Colour panel       { 0xff211a2a };
    const juce::Colour grid        { 0xff2e2638 };
    const juce::Colour text        { 0xffe8e0ee };
    const juce::Colour dimText     { 0xff8d8299 };
    const juce::Colour spectrum    { 0xff7d6f8f };
    const juce::Colour accent      { 0xffe58fa6 }; // soft rose: reduction + knob arcs
}

//==============================================================================
VelourLookAndFeel::VelourLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Colours::dimText);
}

void VelourLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float track = 3.5f;

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::grid);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, angle, true);
    g.setColour (Colours::accent);
    g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = radius - track * 3.0f;
    g.setColour (Colours::panel.brighter (0.15f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -knobR + 4.0f, 3.0f, knobR * 0.45f, 1.5f);
    g.setColour (Colours::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void VelourLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? Colours::accent : Colours::panel.brighter (highlighted ? 0.2f : 0.1f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? Colours::background : Colours::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

//==============================================================================
SpectrumDisplay::SpectrumDisplay (VelourAudioProcessor& p) : processor (p)
{
    startTimerHz (30);
}

void SpectrumDisplay::resized()
{
    const auto w = (size_t) juce::jmax (1, getWidth());
    spectrum.assign (w, minDb);
    reduction.assign (w, 0.0f);
}

float SpectrumDisplay::xForFreq (float hz) const
{
    return (float) getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
}

float SpectrumDisplay::freqForX (float x) const
{
    return minHz * std::pow (maxHz / minHz, x / (float) juce::jmax (1, getWidth()));
}

void SpectrumDisplay::timerCallback()
{
    auto& s = processor.suppressor;
    const int bins = s.meterNumBins.load();
    const float binHz = s.meterBinHz.load();
    if (bins <= 0 || binHz <= 0.0f || spectrum.empty())
        return;

    const int w = (int) spectrum.size();
    for (int x = 0; x < w; ++x)
    {
        const int b0 = juce::jlimit (1, bins - 1, (int) std::floor (freqForX ((float) x) / binHz));
        const int b1 = juce::jlimit (b0, bins - 1, (int) std::floor (freqForX ((float) x + 1.0f) / binHz));

        float spec = -200.0f, red = 0.0f;
        for (int b = b0; b <= b1; ++b)
        {
            spec = juce::jmax (spec, s.meterSpectrumDb[(size_t) b].load (std::memory_order_relaxed));
            red  = juce::jmax (red,  s.meterReductionDb[(size_t) b].load (std::memory_order_relaxed));
        }

        // Fast rise, slow fall, like a meter.
        auto& sp = spectrum[(size_t) x];
        sp = spec > sp ? sp + (spec - sp) * 0.6f : sp + (spec - sp) * 0.15f;
        auto& rd = reduction[(size_t) x];
        rd = red > rd ? rd + (red - rd) * 0.7f : rd + (red - rd) * 0.2f;
    }
    repaint();
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (area, 10.0f);

    // Focus band: dim everything outside it.
    const float lo = processor.apvts.getRawParameterValue (ParamIDs::focusLow)->load();
    const float hi = processor.apvts.getRawParameterValue (ParamIDs::focusHigh)->load();
    g.setColour (Colours::background.withAlpha (0.45f));
    g.fillRect (area.withRight (xForFreq (lo)));
    g.fillRect (area.withLeft (xForFreq (juce::jmax (hi, lo * 1.5f))));

    // Grid
    g.setFont (juce::FontOptions (11.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);
        g.setColour (Colours::grid);
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.setColour (Colours::dimText);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x + 3.0f, area.getBottom() - 16.0f, 40.0f, 14.0f),
                    juce::Justification::left);
    }

    const float h = area.getHeight();
    const int w = (int) spectrum.size();
    if (w < 2) return;

    // Input spectrum (filled)
    juce::Path spec;
    spec.startNewSubPath (0.0f, h);
    for (int x = 0; x < w; ++x)
    {
        const float norm = juce::jlimit (0.0f, 1.0f, (spectrum[(size_t) x] - minDb) / (maxDb - minDb));
        spec.lineTo ((float) x, h - norm * h);
    }
    spec.lineTo ((float) w, h);
    spec.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Colours::spectrum.withAlpha (0.55f), 0.0f, 0.0f,
                                             Colours::spectrum.withAlpha (0.05f), 0.0f, h, false));
    g.fillPath (spec);

    // Reduction curve hanging from the top
    juce::Path red;
    red.startNewSubPath (0.0f, 0.0f);
    for (int x = 0; x < w; ++x)
        red.lineTo ((float) x, juce::jlimit (0.0f, 1.0f, reduction[(size_t) x] / maxRedDb) * h * 0.6f);
    red.lineTo ((float) w, 0.0f);
    red.closeSubPath();
    g.setColour (Colours::accent.withAlpha (0.35f));
    g.fillPath (red);
    g.setColour (Colours::accent);
    g.strokePath (red, juce::PathStrokeType (1.5f));

    g.setColour (Colours::grid.brighter (0.2f));
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.0f);
}

//==============================================================================
VelourAudioProcessorEditor::VelourAudioProcessorEditor (VelourAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), display (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (display);

    addKnob (depth,       ParamIDs::depth,       "DEPTH");
    addKnob (sharpness,   ParamIDs::sharpness,   "SHARPNESS");
    addKnob (selectivity, ParamIDs::selectivity, "SELECTIVITY");
    addKnob (attack,      ParamIDs::attack,      "ATTACK");
    addKnob (release,     ParamIDs::release,     "RELEASE");
    addKnob (focusLow,    ParamIDs::focusLow,    "FOCUS LOW");
    addKnob (focusHigh,   ParamIDs::focusHigh,   "FOCUS HIGH");
    addKnob (mix,         ParamIDs::mix,         "MIX");
    addKnob (output,      ParamIDs::output,      "OUTPUT");

    addAndMakeVisible (deltaButton);
    addAndMakeVisible (linkButton);
    deltaAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ParamIDs::delta, deltaButton);
    linkAttach  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ParamIDs::link, linkButton);

    deltaButton.setTooltip ("Hear only what is being removed");
    setSize (900, 540);
}

VelourAudioProcessorEditor::~VelourAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void VelourAudioProcessorEditor::addKnob (Knob& k, const char* paramId, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, k.slider);
}

void VelourAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("VELOUR", 24, 14, 200, 28, juce::Justification::left);
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("resonance suppressor", 128, 20, 200, 20, juce::Justification::left);
}

void VelourAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    auto header = area.removeFromTop (34);
    linkButton.setBounds (header.removeFromRight (110).reduced (0, 3));
    header.removeFromRight (8);
    deltaButton.setBounds (header.removeFromRight (80).reduced (0, 3));

    area.removeFromTop (8);
    auto knobs = area.removeFromBottom (150);
    area.removeFromBottom (12);
    display.setBounds (area);

    Knob* row[] = { &depth, &sharpness, &selectivity, &attack, &release, &focusLow, &focusHigh, &mix, &output };
    const int n = (int) std::size (row);
    const int kw = knobs.getWidth() / n;
    for (auto* k : row)
    {
        auto cell = knobs.removeFromLeft (kw);
        k->label.setBounds (cell.removeFromTop (18));
        k->slider.setBounds (cell);
    }
}
