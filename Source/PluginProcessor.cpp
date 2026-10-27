#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

HarmonyVoicesProcessor::HarmonyVoicesProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  AudioChannelSet::stereo(), true)
          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{}

AudioProcessorValueTreeState::ParameterLayout HarmonyVoicesProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto addF = [&](const char* id, const char* name, float lo, float hi, float def)
    { p.push_back(std::make_unique<AudioParameterFloat>(id, name, lo, hi, def)); };
    auto addB = [&](const char* id, const char* name, bool def)
    { p.push_back(std::make_unique<AudioParameterBool>(id, name, def)); };

    addF("input_gain",  "Input Gain",  -24.0f, 24.0f, 0.0f);
    addF("output_gain", "Output Gain", -24.0f, 24.0f, 0.0f);
    addF("wet_mix",     "Global Mix",    0.0f, 1.0f, 1.0f);

    addB("pitch_enable", "Pitch Enable", true);
    addF("pitch_retune", "Retune", 0.0f, 400.0f, 20.0f);
    addF("pitch_amount", "Correction", 0.0f, 1.0f, 1.0f);
    addF("pitch_shift",  "Pitch Shift", -24.0f, 24.0f, 0.0f);

    addB("comp_enable", "Comp Enable", false);
    addF("comp_thr",    "Threshold", -60.0f, 0.0f, -18.0f);
    addF("comp_ratio",  "Ratio",       1.0f, 20.0f, 3.0f);

    addB("eq_enable", "EQ Enable", false);
    addF("eq_low",  "Low",  -18.0f, 18.0f, 0.0f);
    addF("eq_mid",  "Mid",  -18.0f, 18.0f, 0.0f);
    addF("eq_high", "High", -18.0f, 18.0f, 0.0f);

    addB("rev_enable", "Reverb Enable", false);
    addF("rev_size",   "Size", 0.0f, 1.0f, 0.5f);
    addF("rev_mix",    "Reverb Mix", 0.0f, 1.0f, 0.25f);

    return { p.begin(), p.end() };
}

void HarmonyVoicesProcessor::prepareToPlay (double sr, int block)
{
    detector.prepare (sr);
    shifter.prepare (sr);
    
    // CORRECTION 1 : initialisation complète de la réverbération
    reverb.prepare ({ sr, (uint32) block, 2 });

    dryBuf.setSize (2, block, false, false, true);
    monoBuf.setSize (1, block, false, false, true);
    scratch.setSize (1, block, false, false, true);

    for (int c = 0; c < 2; ++c)
    {
        eqLow[c].prepare ({ sr, (uint32) block, 1 });
        eqMid[c].prepare ({ sr, (uint32) block, 1 });
        eqHigh[c].prepare ({ sr, (uint32) block, 1 });
    }
    smoothedRatio = 1.0f;
}

void HarmonyVoicesProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int ch = buffer.getNumChannels();
    if (ch < 2) return;

    const float inGain  = Decibels::decibelsToGain (apvts.getRawParameterValue ("input_gain")->load());
    const float outGain = Decibels::decibelsToGain (apvts.getRawParameterValue ("output_gain")->load());
    const float wetMix  = apvts.getRawParameterValue ("wet_mix")->load();

    dryBuf.setSize (ch, n, false, false, true);
    for (int c = 0; c < ch; ++c) dryBuf.copyFrom (c, 0, buffer, c, 0, n);
    for (int c = 0; c < ch; ++c) buffer.applyGain (c, 0, n, inGain);

    monoBuf.setSize (1, n, false, false, true);
    monoBuf.clear();
    for (int c = 0; c < ch; ++c) monoBuf.addFrom (0, 0, buffer, c, 0, n, 1.0f / (float) ch);

    // --- Pitch correction ---
    detector.push (monoBuf.getReadPointer (0), n);
    const float hz = detector.detect();
    if (apvts.getRawParameterValue ("pitch_enable")->load() > 0.5f && hz > 0.0f)
    {
        const float shiftSemis = apvts.getRawParameterValue ("pitch_shift")->load();
        const float amount = apvts.getRawParameterValue ("pitch_amount")->load();
        const float retuneMs = apvts.getRawParameterValue ("pitch_retune")->load();

        const float midiF = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int nearest = (int) std::round (midiF);
        const int deg = (nearest % 12 + 12) % 12;
        static const int major[] = {0, 2, 4, 5, 7, 9, 11};
        int best = major[0], bd = 99;
        for (int i = 0; i < 7; ++i)
            if (std::abs (major[i] - deg) < bd) { bd = std::abs (major[i] - deg); best = major[i]; }
        const int corrMidi = (nearest - deg) + best;
        const float tgtMidi = (float) corrMidi + shiftSemis;

        float ratio = std::pow (2.0f, (tgtMidi - midiF) / 12.0f);
        ratio = 1.0f + (ratio - 1.0f) * amount;
        const float alpha = retuneMs <= 0.01f ? 0.0f
            : std::exp (-(float) n / (0.001f * retuneMs * (float) getSampleRate()));
        smoothedRatio = alpha * smoothedRatio + (1.0f - alpha) * ratio;

        shifter.setRatio (smoothedRatio);
        shifter.setPeriod ((float) getSampleRate() / juce::jmax (40.0f, hz));

        scratch.setSize (1, n, false, false, true);
        shifter.process (monoBuf.getReadPointer (0), scratch.getWritePointer (0), n);
        for (int c = 0; c < ch; ++c) buffer.copyFrom (c, 0, scratch, 0, 0, n);
    }

    // --- Compressor ---
    if (apvts.getRawParameterValue ("comp_enable")->load() > 0.5f)
    {
        const float thr = apvts.getRawParameterValue ("comp_thr")->load();
        const float rat = juce::jmax (1.0f, apvts.getRawParameterValue ("comp_ratio")->load());
        auto* L = buffer.getWritePointer (0);
        auto* R = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float det = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            const float detDb = Decibels::gainToDecibels (det + 1e-9f);
            const float over = detDb - thr;
            const float yDb = over > 0.0f ? over * (1.0f / rat - 1.0f) : 0.0f;
            const float gain = Decibels::decibelsToGain (yDb);
            L[i] *= gain;
            R[i] *= gain;
        }
    }

    // --- EQ ---
    if (apvts.getRawParameterValue ("eq_enable")->load() > 0.5f)
    {
        const double sr = getSampleRate();
        const float lowG  = Decibels::decibelsToGain (apvts.getRawParameterValue ("eq_low")->load());
        const float midG  = Decibels::decibelsToGain (apvts.getRawParameterValue ("eq_mid")->load());
        const float highG = Decibels::decibelsToGain (apvts.getRawParameterValue ("eq_high")->load());
        for (int c = 0; c < 2; ++c)
        {
            // CORRECTION 2 : ajout de "juce::dsp::" devant IIR
            *eqLow[c].coefficients  = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, 200.0f, 0.707f, lowG);
            *eqMid[c].coefficients  = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 1500.0f, 1.0f, midG);
            *eqHigh[c].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 6000.0f, 0.707f, highG);
        }
        for (int c = 0; c < 2; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                float s = d[i];
                s = eqLow[c].processSample (s);
                s = eqMid[c].processSample (s);
                s = eqHigh[c].processSample (s);
                d[i] = s;
            }
        }
    }

    // --- Reverb ---
    if (apvts.getRawParameterValue ("rev_enable")->load() > 0.5f)
    {
        Reverb::Parameters rp;
        rp.roomSize = apvts.getRawParameterValue ("rev_size")->load();
        rp.damping = 0.5f;
        rp.wetLevel = apvts.getRawParameterValue ("rev_mix")->load();
        rp.dryLevel = 1.0f - rp.wetLevel;
        rp.width = 1.0f;
        reverb.setParameters (rp);
        
        // CORRECTION 3 : nouvelle méthode de traitement de la réverbération
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);
        reverb.process (context);
    }

    // --- Global wet/dry + output ---
    for (int c = 0; c < ch; ++c)
    {
        buffer.applyGain (c, 0, n, wetMix);
        buffer.addFrom (c, 0, dryBuf, c, 0, n, 1.0f - wetMix);
        buffer.applyGain (c, 0, n, outGain);
    }
}

void HarmonyVoicesProcessor::getStateInformation (MemoryBlock& dest)
{
    auto xml = apvts.copyState().createXml();
    copyXmlToBinary (*xml, dest);
}

void HarmonyVoicesProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml) apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* HarmonyVoicesProcessor::createEditor()
{
    return new HarmonyVoicesEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonyVoicesProcessor();
}
