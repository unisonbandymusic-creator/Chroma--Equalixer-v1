#pragma once
#include "PluginProcessor.h"

static const juce::Colour kG   = juce::Colour::fromRGB (57, 255, 20);
static const juce::Colour kRed = juce::Colour::fromRGB (255, 42, 61);
static const juce::Colour kWht = juce::Colour::fromRGB (244, 255, 244);

struct NeonLnF : public juce::LookAndFeel_V4
{
    NeonLnF()
    {
        setColour (juce::TextButton::textColourOffId, kG); setColour (juce::TextButton::textColourOnId, kWht);
        setColour (juce::Slider::textBoxTextColourId, kG); setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::thumbColourId, kG); setColour (juce::Slider::trackColourId, kG.withAlpha (0.55f));
        setColour (juce::Slider::backgroundColourId, kG.withAlpha (0.18f));
        setColour (juce::TextEditor::backgroundColourId, juce::Colours::black); setColour (juce::TextEditor::textColourId, juce::Colour (0xffa8ffa6));
        setColour (juce::TextEditor::outlineColourId, kG.withAlpha (0.25f)); setColour (juce::TextEditor::focusedOutlineColourId, kG.withAlpha (0.25f));
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.f); const bool on = b.getToggleState();
        g.setColour (kG.withAlpha (on ? 0.18f : (over ? 0.10f : 0.05f))); g.fillRoundedRectangle (r, 8.f);
        g.setColour (on ? kG : kG.withAlpha (0.55f)); g.drawRoundedRectangle (r, 8.f, on ? 1.8f : 1.f);
    }
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (8.f);
        const float cx = r.getCentreX(), cy = r.getCentreY(), rad = juce::jmin (r.getWidth(), r.getHeight()) / 2.f, a = a0 + pos * (a1 - a0);
        const bool isGain = s.getName().startsWithChar ('g');
        const auto col = isGain ? (s.getValue() < -0.04 ? kRed : (s.getValue() > 0.04 ? kWht : kG)) : kG;
        juce::Path bg, v; bg.addCentredArc (cx, cy, rad, rad, 0.f, a0, a1, true); v.addCentredArc (cx, cy, rad, rad, 0.f, a0, a, true);
        g.setColour (kG.withAlpha (0.18f)); g.strokePath (bg, juce::PathStrokeType (3.f));
        g.setColour (col); g.strokePath (v, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine (cx + (rad - 12) * std::sin (a), cy - (rad - 12) * std::cos (a), cx + (rad - 2) * std::sin (a), cy - (rad - 2) * std::cos (a), 2.5f);
    }
};

class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (ChromaEqProcessor& p);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    ChromaEqProcessor& proc;
    juce::dsp::FFT fft { 12 };
    std::array<float, 8192> buf {};
    std::array<float, 4096> win {};
    std::array<float, 2048> smooth, dry;
};

class ChromaEqEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ChromaEqEditor (ChromaEqProcessor&);
    ~ChromaEqEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    ChromaEqProcessor& proc;
    NeonLnF lnf;
    SpectrumView spec;
    juce::TextButton swDry { "DRY - ORIGINAL" }, swWet { "WET - PROCESSED" },
                     modeA { "MODE A  MUD/HARSH" }, modeB { "MODE B  +TONE BOOST" },
                     iLow { "LOW" }, iMid { "MID" }, iHigh { "HIGH" },
                     analyzeBtn { "ANALYZE AUDIO" }, resetBtn { "RESET CURVE" };
    juce::Slider fS[5], gS[5], outS;
    std::vector<std::unique_ptr<SA>> atts;
    juce::TextEditor logBox;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaEqEditor)
};
