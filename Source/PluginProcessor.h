#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>

// ============ YIN Pitch Detector ============
class YinDetector
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        tauMax = juce::jmin (1024, (int) (sr / 60.0));
        buf.fill (0.0f);
        writePos = 0;
        lastFreq = 0.0f;
    }

    void push (const float* data, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            buf[(size_t) writePos] = data[i];
            writePos = (writePos + 1) % kBufSize;
        }
    }

    float detect()
    {
        std::vector<float> lin ((size_t) (kWindow + tauMax));
        const int start = (writePos - kWindow - tauMax + kBufSize * 4) % kBufSize;
        for (int i = 0; i < kWindow + tauMax; ++i)
            lin[(size_t) i] = buf[(size_t) ((start + i) % kBufSize)];

        std::vector<float> d ((size_t) tauMax + 1, 0.0f);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < kWindow; ++j)
            {
                const float diff = lin[(size_t) j] - lin[(size_t) (j + tau)];
                sum += diff * diff;
            }
            d[(size_t) tau] = sum;
        }
        std::vector<float> cmnd ((size_t) tauMax + 1, 1.0f);
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += d[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0f ? d[(size_t) tau] * (float) tau / running : 1.0f;
        }
        int best = -1;
        for (int tau = 2; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 1) { lastFreq = 0.0f; return 0.0f; }
        const float y0 = cmnd[(size_t) (best - 1)];
        const float y1 = cmnd[(size_t) best];
        const float y2 = cmnd[(size_t) (best + 1)];
        const float denom = y0 - 2.0f * y1 + y2;
        const float shift = denom != 0.0f ? 0.5f * (y0 - y2) / denom : 0.0f;
        const float tauI = (float) best + shift;
        lastFreq = (float) sampleRate / tauI;
        if (lastFreq < 40.0f || lastFreq > 1500.0f) return 0.0f;
        return lastFreq;
    }

private:
    static constexpr int kBufSize = 4096;
    static constexpr int kWindow = 1024;
    std::array<float, kBufSize> buf {};
    int writePos = 0;
    int tauMax = 735;
    double sampleRate = 44100.0;
    float lastFreq = 0.0f;
};

// ============ PSOLA Pitch Shifter ============
class PsolaShifter
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        maxPeriod = juce::jlimit (64, 2048, (int) (sr / 30.0));
        maxHalf = maxPeriod * 4;
        delay = 2 * maxHalf + 128;
        bufSize = juce::nextPowerOfTwo (4 * delay);
        inBuf.assign ((size_t) bufSize, 0.0f);
        outBuf.assign ((size_t) bufSize, 0.0f);
        reset();
    }
    void reset()
    {
        std::fill (inBuf.begin(), inBuf.end(), 0.0f);
        std::fill (outBuf.begin(), outBuf.end(), 0.0f);
        inWrite = 0;
        readPos = (bufSize - delay) % bufSize;
        nextSynthPos = 0.0f;
        srcPeriod = 200.0f;
        ratio = 1.0f;
    }
    void setRatio (float r) { ratio = juce::jlimit (0.25f, 4.0f, r); }
    void setPeriod (float s) { srcPeriod = juce::jlimit (20.0f, (float) maxPeriod, s); }

    void process (const float* in, float* out, int n)
    {
        const float Pdst = srcPeriod / ratio;
        const int half = (int) juce::jmax (srcPeriod, Pdst);
        for (int i = 0; i < n; ++i)
        {
            inBuf[(size_t) inWrite] = in[i];
            while (nextSynthPos + (float) half <= (float) inWrite)
            {
                const int oc = (int) nextSynthPos;
                const int ac = (int) (std::round (nextSynthPos / srcPeriod) * srcPeriod);
                emitGrain (ac, oc, half);
                nextSynthPos += Pdst;
            }
            out[i] = outBuf[(size_t) readPos];
            outBuf[(size_t) readPos] = 0.0f;
            inWrite = (inWrite + 1) % bufSize;
            readPos = (readPos + 1) % bufSize;
        }
    }

private:
    void emitGrain (int ac, int oc, int half)
    {
        const float scale = juce::jmin (1.0f, (srcPeriod / ratio) / (float) half);
        for (int g = -half; g <= half; ++g)
        {
            const float win = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * g / (float) half));
            const int rp = (ac + g + bufSize * 8) % bufSize;
            const int op = (oc + g + bufSize * 8) % bufSize;
            outBuf[(size_t) op] += inBuf[(size_t) rp] * win * scale;
        }
    }
    std::vector<float> inBuf, outBuf;
    int bufSize = 0, inWrite = 0, readPos = 0, delay = 0, maxHalf = 0, maxPeriod = 0;
    float nextSynthPos = 0.0f, srcPeriod = 200.0f, ratio = 1.0f;
    double sampleRate = 44100.0;
};

// ============ Main Processor ============
class HarmonyVoicesProcessor : public juce::AudioProcessor
{
public:
    HarmonyVoicesProcessor();
    ~HarmonyVoicesProcessor() override = default;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "HarmonyVoices"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    YinDetector detector;
    PsolaShifter shifter;
    juce::dsp::Reverb reverb;
    juce::dsp::IIR::Filter<float> eqLow[2], eqMid[2], eqHigh[2];
    juce::AudioBuffer<float> dryBuf;
    juce::AudioBuffer<float> monoBuf;
    juce::AudioBuffer<float> scratch;
    float smoothedRatio = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonyVoicesProcessor)
};
