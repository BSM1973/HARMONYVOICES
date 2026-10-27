#include "PluginEditor.h"

using namespace juce;

HarmonyVoicesEditor::HarmonyVoicesEditor (HarmonyVoicesProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (900, 620);

    titleLabel.setText ("LIBERTY   |   HARMONYVOICES", dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
    titleLabel.setColour (Label::textColourId, Colour (0xffe3c9a1));
    addAndMakeVisible (titleLabel);

    grpPitch .setText ("PITCH CORRECTION");
    grpComp  .setText ("COMPRESSOR");
    grpEQ    .setText ("EQ");
    grpReverb.setText ("REVERB");
    grpMaster.setText ("MASTER");
    for (auto* g : { &grpPitch, &grpComp, &grpEQ, &grpReverb, &grpMaster })
    {
        g->setColour (GroupComponent::outlineColourId, Colour (0xff2c4058));
        g->setColour (GroupComponent::textColourId, Colour (0xff39e6d6));
        addAndMakeVisible (g);
    }

    pitchOn.setButtonText ("ON");
    compOn .setButtonText ("ON");
    eqOn   .setButtonText ("ON");
    revOn  .setButtonText ("ON");
    addAndMakeVisible (pitchOn);
    addAndMakeVisible (compOn);
    addAndMakeVisible (eqOn);
    addAndMakeVisible (revOn);

    pitchOn .setToggleState (true, dontSendNotification);
    pitchOn .onClick = [this] { if (auto* p = proc.apvts.getParameter ("pitch_enable")) p->setValueNotifyingHost (pitchOn.getToggleState() ? 1.0f : 0.0f); };
    compOn  .onClick = [this] { if (auto* p = proc.apvts.getParameter ("comp_enable"))  p->setValueNotifyingHost (compOn.getToggleState() ? 1.0f : 0.0f); };
    eqOn    .onClick = [this] { if (auto* p = proc.apvts.getParameter ("eq_enable"))    p->setValueNotifyingHost (eqOn.getToggleState() ? 1.0f : 0.0f); };
    revOn   .onClick = [this] { if (auto* p = proc.apvts.getParameter ("rev_enable"))   p->setValueNotifyingHost (revOn.getToggleState() ? 1.0f : 0.0f); };

    addKnob (grpPitch, "pitch_retune", "Retune");
    addKnob (grpPitch, "pitch_amount", "Amount");
    addKnob (grpPitch, "pitch_shift",  "Shift");

    addKnob (grpComp, "comp_thr",   "Thresh");
    addKnob (grpComp, "comp_ratio", "Ratio");

    addKnob (grpEQ, "eq_low",  "Low");
    addKnob (grpEQ, "eq_mid",  "Mid");
    addKnob (grpEQ, "eq_high", "High");

    addKnob (grpReverb, "rev_size", "Size");
    addKnob (grpReverb, "rev_mix",  "Mix");

    addKnob (grpMaster, "input_gain",  "In");
    addKnob (grpMaster, "output_gain", "Out");
    addKnob (grpMaster, "wet_mix",     "Mix");
}

HarmonyVoicesEditor::~HarmonyVoicesEditor()
{
    setLookAndFeel (nullptr);
}

void HarmonyVoicesEditor::addKnob (Component& parent, const String& id, const String& label)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (Slider::TextBoxBelow, false, 60, 14);
    k->label.setText (label, dontSendNotification);
    k->label.setJustificationType (Justification::centred);
    k->label.setFont (juce::Font (juce::FontOptions (11.0f)));
    k->label.setColour (Label::textColourId, Colour (0xffb9cadb));
    parent.addAndMakeVisible (k->slider);
    parent.addAndMakeVisible (k->label);
    k->att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    knobs.push_back (std::move (k));
}

void HarmonyVoicesEditor::paint (Graphics& g)
{
    ColourGradient bg (Colour (0xff101d2e), 0.0f, 0.0f,
                       Colour (0xff060b14), 0.0f, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    g.setColour (Colour (0xff39e6d6).withAlpha (0.10f));
    g.fillRect (0, 0, getWidth(), 2);

    g.setColour (Colour (0xff2c4058));
    g.drawLine (0.0f, 58.0f, (float) getWidth(), 58.0f, 1.0f);
}

void HarmonyVoicesEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (44);
    titleLabel.setBounds (header);

    area.removeFromTop (10);

    auto row1 = area.removeFromTop (150);
    auto row2 = area.removeFromTop (150);
    auto row3 = area.removeFromTop (150);

    auto rowA = row1;
    auto pR = rowA.removeFromLeft (rowA.getWidth() * 55 / 100);
    auto cR = rowA;
    
    auto rowB = row2;
    auto eR = rowB.removeFromLeft (rowB.getWidth() * 55 / 100);
    auto vR = rowB;
    
    auto rowC = row3;
    auto mR = rowC;

    // CORRECTION ICI : Utilisation de coordonnées relatives (0,0)
    auto place = [&](GroupComponent& grp, std::initializer_list<int> idxs, Rectangle<int> r)
    {
        grp.setBounds (r);
        
        // On crée un rectangle interne basé sur (0,0) pour le groupe
        juce::Rectangle<int> inner (0, 0, r.getWidth(), r.getHeight());
        inner.reduce (10, 10);
        inner.removeFromTop (18);
        
        const int cellW = inner.getWidth() / (int) idxs.size();
        const int cellH = inner.getHeight() - 20;
        int cx = inner.getX();
        
        for (auto i : idxs)
        {
            if (i < 0 || i >= (int) knobs.size()) continue;
            auto& k = *knobs[(size_t) i];
            Rectangle<int> cell (cx, inner.getY(), cellW, cellH);
            k.label .setBounds (cell.removeFromTop (14));
            k.slider.setBounds (cell.reduced (2));
            cx += cellW;
        }
    };

    place (grpPitch,  {0, 1, 2},     pR.reduced (4));
    place (grpComp,   {3, 4},        cR.reduced (4));
    place (grpEQ,     {5, 6, 7},     eR.reduced (4));
    place (grpReverb, {8, 9},        vR.reduced (4));
    place (grpMaster, {10, 11, 12},  mR.reduced (4));

    // Les boutons ON restent en coordonnées absolues car ils appartiennent au composant principal (Editor)
    pitchOn.setBounds (grpPitch .getRight() - 50, grpPitch .getY() + 4, 40, 18);
    compOn .setBounds (grpComp  .getRight() - 50, grpComp  .getY() + 4, 40, 18);
    eqOn   .setBounds (grpEQ    .getRight() - 50, grpEQ    .getY() + 4, 40, 18);
    revOn  .setBounds (grpReverb.getRight() - 50, grpReverb.getY() + 4, 40, 18);
}
