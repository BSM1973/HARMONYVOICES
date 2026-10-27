#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class HarmonyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HarmonyLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff0b1322));
        setColour (juce::Label::textColourId,                 juce::Colour (0xffd7e6f2));
        setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff39e6d6));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff1c2a3a));
        setColour (juce::Slider::thumbColourId,               juce::Colour (0xffe3c9a1));
        setColour (juce::TextButton::buttonColourId,          juce::Colour (0xff1b2a3e));
        setColour (juce::TextButton::textColourOnId,          juce::Colour (0xff39e6d6));
        setColour (juce::TextButton::textColourOffId,         juce::Colour (0xffb9cadb));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider&) override
    {
        const float radius = (float) juce::jmin (w, h) * 0.5f - 3.0f;
        const auto centre = juce::Point<float> ((float) x + w * 0.5f, (float) y + h * 0.5f);
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::ColourGradient bezel (juce::Colour (0xff3a4d63), centre.x, centre.y - radius,
                                    juce::Colour (0xff0d1622), centre.x, centre.y + radius, false);
        g.setGradientFill (bezel);
        g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

        juce::ColourGradient face (juce::Colour (0xff1d2b3d), centre.x, centre.y - radius * 0.8f,
                                   juce::Colour (0xff0a111b), centre.x, centre.y + radius * 0.8f, false);
        g.setGradientFill (face);
        g.fillEllipse (centre.x - radius * 0.8f, centre.y - radius * 0.8f,
                       radius * 1.6f, radius * 1.6f);

        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f,
                           startAngle, angle, true);
        g.setColour (juce::Colour (0xff39e6d6).withAlpha (0.85f));
        g.strokePath (arc, juce::PathStrokeType (2.5f));

        const auto tip = centre.getPointOnCircumference (radius * 0.72f, angle);
        g.setColour (juce::Colour (0xffe3c9a1));
        g.drawLine (juce::Line<float> (centre, tip), 2.0f);
    }
};

class HarmonyVoicesEditor : public juce::AudioProcessorEditor
{
public:
    explicit HarmonyVoicesEditor (HarmonyVoicesProcessor&);
    ~HarmonyVoicesEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    void addKnob (juce::Component& parent, const juce::String& id, const juce::String& label);

    HarmonyVoicesProcessor& proc;
    HarmonyLookAndFeel lnf;

    juce::Label titleLabel;
    juce::GroupComponent grpPitch, grpComp, grpEQ, grpReverb, grpMaster;
    juce::ToggleButton pitchOn, compOn, eqOn, revOn;

    std::vector<std::unique_ptr<Knob>> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonyVoicesEditor)
};
