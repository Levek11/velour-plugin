// Velour — dynamic resonance suppressor DSP core.
// Self-contained (no JUCE dependency) so it can be unit-tested anywhere.
//
// How it works (per STFT frame):
//   1. Window + FFT each channel (Hann, 87.5% overlap).
//   2. Build a "detail" spectrum (local energy over a narrow band whose
//      width is set by Sharpness) and a broad 1.5-octave "baseline".
//   3. Wherever the detail rises above the baseline by more than the
//      Selectivity threshold, that region is treated as a resonance.
//   4. Reduction = excess scaled by Depth (capped at Depth dB), weighted
//      by the Focus band, smoothed across frequency, then smoothed in
//      time per bin with Attack / Release.
//   5. Apply the gains, inverse FFT, overlap-add. Mix and Delta are done
//      in the spectral domain, so dry and wet stay phase-aligned.

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

#ifndef M_PI
 #define M_PI 3.14159265358979323846
#endif

namespace velour
{

//==============================================================================
// Small iterative radix-2 complex FFT. Fast enough for this use
// (a few % of one core at 48 kHz stereo) and keeps the core portable.
class FFT
{
public:
    void prepare (int n)
    {
        size = n;
        levels = 0;
        while ((1 << levels) < n) ++levels;

        bitrev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < levels; ++b)
                if (i & (1 << b)) r |= 1 << (levels - 1 - b);
            bitrev[(size_t) i] = r;
        }

        twiddle.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * M_PI * i / n;
            twiddle[(size_t) i] = { (float) std::cos (a), (float) std::sin (a) };
        }
    }

    // In-place. inverse = true computes the unnormalised inverse.
    void perform (std::complex<float>* d, bool inverse) const
    {
        for (int i = 0; i < size; ++i)
        {
            const int j = bitrev[(size_t) i];
            if (j > i) std::swap (d[i], d[j]);
        }

        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len / 2;
            const int step = size / len;
            for (int i = 0; i < size; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    auto w = twiddle[(size_t) (k * step)];
                    if (inverse) w = std::conj (w);
                    const auto u = d[i + k];
                    const auto v = d[i + k + half] * w;
                    d[i + k] = u + v;
                    d[i + k + half] = u - v;
                }
            }
        }
    }

private:
    int size = 0, levels = 0;
    std::vector<int> bitrev;
    std::vector<std::complex<float>> twiddle;
};

//==============================================================================
struct SuppressorParams
{
    float depthDb      = 6.0f;     // 0 .. 20   max reduction / aggressiveness
    float sharpness    = 4.0f;     // 1 .. 10   higher = narrower, more surgical cuts
    float selectivity  = 4.0f;     // 1 .. 10   higher = only the most prominent peaks
    float attackMs     = 5.0f;     // 0.5 .. 50
    float releaseMs    = 60.0f;    // 5 .. 500
    float focusLowHz   = 800.0f;   // processing band, soft edges
    float focusHighHz  = 16000.0f;
    float mix          = 1.0f;     // 0 .. 1
    bool  delta        = false;    // listen to only what is being removed
    bool  stereoLink   = true;     // same reduction on both channels
    float outputDb     = 0.0f;
};

//==============================================================================
class ResonanceSuppressor
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double newSampleRate, int numChannelsIn)
    {
        sampleRate  = newSampleRate;
        numChannels = std::clamp (numChannelsIn, 1, maxChannels);

        // Keep roughly constant frequency resolution (~23 Hz) across rates.
        fftSize = sampleRate <= 50000.0 ? 2048 : (sampleRate <= 100000.0 ? 4096 : 8192);
        hop     = fftSize / 8;
        numBins = fftSize / 2 + 1;

        fft.prepare (fftSize);

        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * M_PI * i / fftSize);

        // Overlap-add normalisation for analysis * synthesis Hann windows
        // (the sum of w^2 over overlapping frames is constant).
        double olaGain = 0.0;
        for (int m = 0; m < fftSize / hop; ++m)
        {
            const float w = window[(size_t) (m * hop)];
            olaGain += (double) w * w;
        }
        olaNorm = (float) (1.0 / (olaGain * fftSize)); // includes 1/N for the inverse FFT

        double wsum = 0.0;
        for (auto w : window) wsum += w;
        powerNorm = (float) (1.0 / ((wsum * 0.5) * (wsum * 0.5))); // full-scale sine ≈ 0 dB

        binLog2Hz.resize ((size_t) numBins);
        for (int k = 0; k < numBins; ++k)
            binLog2Hz[(size_t) k] = (float) std::log2 (std::max (1.0, k * sampleRate / fftSize));

        for (int c = 0; c < maxChannels; ++c)
        {
            inRing[c].assign ((size_t) fftSize, 0.0f);
            outRing[c].assign ((size_t) fftSize, 0.0f);
            spec[c].assign ((size_t) fftSize, {});
            powerDb[c].assign ((size_t) numBins, -200.0f);
            reductionState[c].assign ((size_t) numBins, 0.0f);
        }

        analysisDb.assign ((size_t) numBins, 0.0f);
        envelopeDb.assign ((size_t) numBins, 0.0f);
        detailDb.assign ((size_t) numBins, 0.0f);
        scratch.assign ((size_t) numBins, 0.0f);
        rawReduction.assign ((size_t) numBins, 0.0f);
        smoothReduction.assign ((size_t) numBins, 0.0f);
        focusWeight.assign ((size_t) numBins, 0.0f);
        prefix.assign ((size_t) numBins + 1, 0.0);

        for (int k = 0; k < maxMeterBins; ++k)
        {
            meterSpectrumDb[(size_t) k].store (-120.0f);
            meterReductionDb[(size_t) k].store (0.0f);
        }
        meterNumBins.store (numBins);
        meterBinHz.store ((float) (sampleRate / fftSize));

        writePos = 0;
        hopCounter = 0;
        lastFocusLow = lastFocusHigh = -1.0f;
        setParams (params);
    }

    void setParams (const SuppressorParams& p)
    {
        params = p;
        if (fftSize == 0) return;

        const double frameSeconds = (double) hop / sampleRate;
        attackCoef  = (float) std::exp (-frameSeconds / std::max (1.0e-4, params.attackMs * 0.001));
        releaseCoef = (float) std::exp (-frameSeconds / std::max (1.0e-4, params.releaseMs * 0.001));

        // Sharpness 1..10 -> detection bandwidth 1/2 octave .. 1/24 octave
        const float s = std::clamp ((params.sharpness - 1.0f) / 9.0f, 0.0f, 1.0f);
        detailWidthOct = 0.5f * std::pow (1.0f / 12.0f, s);

        // Selectivity 1..10 -> threshold 1.5 .. 12 dB above baseline
        thresholdDb = 1.5f + (std::clamp (params.selectivity, 1.0f, 10.0f) - 1.0f) * (10.5f / 9.0f);

        if (params.focusLowHz != lastFocusLow || params.focusHighHz != lastFocusHigh)
            updateFocusWeights();

        outputGain = std::pow (10.0f, params.outputDb / 20.0f);
    }

    int getLatencySamples() const noexcept { return fftSize; }
    int getNumBins() const noexcept        { return numBins; }
    double getBinFrequency (int k) const   { return k * sampleRate / fftSize; }

    // Interleaving-free processing: channels[c][n]
    void process (float* const* channels, int numSamples)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                inRing[c][(size_t) writePos] = channels[c][n];
                channels[c][n] = outRing[c][(size_t) writePos] * outputGain;
                outRing[c][(size_t) writePos] = 0.0f;
            }

            writePos = (writePos + 1) % fftSize;

            if (++hopCounter == hop)
            {
                hopCounter = 0;
                processFrame();
            }
        }
    }

    // UI metering (written on the audio thread, read by the editor).
    // Fixed-size so the editor can never read freed memory.
    static constexpr int maxMeterBins = 8192 / 2 + 1;
    std::vector<std::atomic<float>> meterSpectrumDb  = std::vector<std::atomic<float>> ((size_t) maxMeterBins);
    std::vector<std::atomic<float>> meterReductionDb = std::vector<std::atomic<float>> ((size_t) maxMeterBins);
    std::atomic<int>   meterNumBins { 0 };
    std::atomic<float> meterBinHz { 23.4f };

private:
    void updateFocusWeights()
    {
        lastFocusLow  = params.focusLowHz;
        lastFocusHigh = params.focusHighHz;
        const float lo = std::log2 (std::max (20.0f, params.focusLowHz));
        const float hi = std::log2 (std::max (params.focusLowHz + 1.0f, params.focusHighHz));
        const float edge = 0.5f; // half-octave soft edges

        auto smooth = [] (float x) { x = std::clamp (x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };

        for (int k = 0; k < numBins; ++k)
        {
            const float f = binLog2Hz[(size_t) k];
            const float up   = smooth ((f - (lo - edge)) / edge);
            const float down = smooth (((hi + edge) - f) / edge);
            focusWeight[(size_t) k] = k == 0 ? 0.0f : up * down;
        }
    }

    // Average `src` across a window of +/- widthOct/2 octaves around each bin.
    void logFrequencyAverage (const std::vector<float>& src, std::vector<float>& dst, float widthOct)
    {
        prefix[0] = 0.0;
        for (int k = 0; k < numBins; ++k)
            prefix[(size_t) k + 1] = prefix[(size_t) k] + src[(size_t) k];

        const float ratio = std::pow (2.0f, widthOct * 0.5f);
        for (int k = 0; k < numBins; ++k)
        {
            int lo = (int) std::floor (k / ratio);
            int hi = (int) std::ceil (k * ratio);
            lo = std::clamp (std::min (lo, k - 1), 1, numBins - 1);
            hi = std::clamp (std::max (hi, k + 1), 1, numBins - 1);
            if (k == 0) { lo = 0; hi = 1; }
            dst[(size_t) k] = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (hi - lo + 1));
        }
    }

    void computeReduction (const std::vector<float>& inputDb, std::vector<float>& state)
    {
        // Detail spectrum: local energy (power average) over a narrow band set by
        // Sharpness. This merges individual harmonics of tonal sources so that
        // only resonant *regions* stand out, not every partial of a note.
        for (int k = 0; k < numBins; ++k)
            scratch[(size_t) k] = std::pow (10.0f, inputDb[(size_t) k] * 0.1f);
        logFrequencyAverage (scratch, detailDb, detailWidthOct);
        for (int k = 0; k < numBins; ++k)
            detailDb[(size_t) k] = 10.0f * std::log10 (detailDb[(size_t) k] + 1.0e-20f);

        // Baseline: broad average in dB (two box passes ≈ smooth kernel).
        logFrequencyAverage (detailDb, scratch, baselineWidthOct);
        logFrequencyAverage (scratch, envelopeDb, baselineWidthOct * 0.5f);

        const float depth = std::max (0.0f, params.depthDb);
        const float slope = depth / 6.0f;

        for (int k = 0; k < numBins; ++k)
        {
            const float level = detailDb[(size_t) k];
            // Fade processing out for very quiet bins (noise floor / tails).
            const float gate = std::clamp ((level + 90.0f) / 20.0f, 0.0f, 1.0f);
            const float excess = level - envelopeDb[(size_t) k] - thresholdDb;
            const float r = std::clamp (excess * slope, 0.0f, depth);
            rawReduction[(size_t) k] = r * gate * focusWeight[(size_t) k];
        }

        // Soften the notch shapes slightly to avoid "musical noise".
        logFrequencyAverage (rawReduction, smoothReduction, detailWidthOct * 0.5f);

        for (int k = 0; k < numBins; ++k)
        {
            const float target = std::max (smoothReduction[(size_t) k], rawReduction[(size_t) k] * 0.85f);
            float& st = state[(size_t) k];
            const float coef = target > st ? attackCoef : releaseCoef;
            st = target + (st - target) * coef;
        }
    }

    void processFrame()
    {
        // 1. Analysis
        for (int c = 0; c < numChannels; ++c)
        {
            auto& X = spec[c];
            for (int i = 0; i < fftSize; ++i)
            {
                const int idx = (writePos + i) % fftSize; // oldest sample first
                X[(size_t) i] = { inRing[c][(size_t) idx] * window[(size_t) i], 0.0f };
            }
            fft.perform (X.data(), false);

            for (int k = 0; k < numBins; ++k)
                powerDb[c][(size_t) k] = 10.0f * std::log10 (std::norm (X[(size_t) k]) * powerNorm + 1.0e-20f);
        }

        // 2. Reduction curves
        const bool linked = params.stereoLink || numChannels == 1;
        if (linked)
        {
            for (int k = 0; k < numBins; ++k)
            {
                float p = 0.0f;
                for (int c = 0; c < numChannels; ++c)
                    p += std::pow (10.0f, powerDb[c][(size_t) k] * 0.1f);
                analysisDb[(size_t) k] = 10.0f * std::log10 (p / numChannels + 1.0e-20f);
            }
            computeReduction (analysisDb, reductionState[0]);
            for (int c = 1; c < numChannels; ++c)
                reductionState[c] = reductionState[0];
        }
        else
        {
            for (int c = 0; c < numChannels; ++c)
                computeReduction (powerDb[c], reductionState[c]);
        }

        // 3. Apply gains, mix / delta in the spectral domain, resynthesise.
        const float mix = std::clamp (params.mix, 0.0f, 1.0f);
        for (int c = 0; c < numChannels; ++c)
        {
            auto& X = spec[c];
            const auto& red = reductionState[c];
            for (int k = 0; k < numBins; ++k)
            {
                const float g = std::pow (10.0f, -red[(size_t) k] / 20.0f);
                const float wet = params.delta ? (1.0f - g) : g;
                const float gain = params.delta ? wet * mix : mix * wet + (1.0f - mix);
                X[(size_t) k] *= gain;
                if (k > 0 && k < fftSize / 2)
                    X[(size_t) (fftSize - k)] *= gain;
            }

            fft.perform (X.data(), true);

            auto& out = outRing[c];
            for (int i = 0; i < fftSize; ++i)
            {
                const int idx = (writePos + i) % fftSize;
                out[(size_t) idx] += X[(size_t) i].real() * window[(size_t) i] * olaNorm;
            }
        }

        // 4. Meters (channel-averaged spectrum, max reduction)
        for (int k = 0; k < numBins; ++k)
        {
            float p = 0.0f, r = 0.0f;
            for (int c = 0; c < numChannels; ++c)
            {
                p += std::pow (10.0f, powerDb[c][(size_t) k] * 0.1f);
                r = std::max (r, reductionState[c][(size_t) k]);
            }
            meterSpectrumDb[(size_t) k].store (10.0f * std::log10 (p / numChannels + 1.0e-20f), std::memory_order_relaxed);
            meterReductionDb[(size_t) k].store (r, std::memory_order_relaxed);
        }
    }

    SuppressorParams params;
    FFT fft;

    double sampleRate = 48000.0;
    int numChannels = 2, fftSize = 0, hop = 0, numBins = 0;
    int writePos = 0, hopCounter = 0;

    std::vector<float> window, binLog2Hz;
    float olaNorm = 1.0f, powerNorm = 1.0f, outputGain = 1.0f;

    std::vector<float> inRing[maxChannels], outRing[maxChannels];
    std::vector<std::complex<float>> spec[maxChannels];
    std::vector<float> powerDb[maxChannels], reductionState[maxChannels];

    std::vector<float> analysisDb, envelopeDb, detailDb, scratch, rawReduction, smoothReduction, focusWeight;
    std::vector<double> prefix;

    float attackCoef = 0.0f, releaseCoef = 0.0f;
    float detailWidthOct = 0.2f, thresholdDb = 4.0f;
    static constexpr float baselineWidthOct = 1.5f;
    float lastFocusLow = -1.0f, lastFocusHigh = -1.0f;
};

} // namespace velour
