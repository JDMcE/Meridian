#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Parameters.h"

namespace mrdn
{

// Up to 6-band EQ applied to the detector (sidechain) signal, at the host
// rate, in the M/S domain — each band targets Both, Mid, or Side. RBJ
// biquads for the standard shapes; the brickwall cuts are 16th-order
// Butterworth cascades (8 biquads).
class SidechainEq
{
public:
    struct Band
    {
        bool on = false;
        ScFilterType type = ScFilterType::bell;
        float freq = 1000.0f;
        float gainDb = 0.0f;
        float q = 0.71f;
        int chan = 0; // 0 both, 1 mid, 2 side
    };

    static constexpr int brickwallStages = 8; // order 16

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& band : states)
            for (auto& chan : band)
                for (auto& stage : chan)
                    stage = {};
    }

    void setParams (const std::array<Band, numScBands>& newBands)
    {
        bands = newBands;
        for (int b = 0; b < numScBands; ++b)
            computeCoeffs (b);
    }

    bool anyBandActive() const noexcept
    {
        for (const auto& b : bands)
            if (b.on)
                return true;
        return false;
    }

    // mid/side channel pointers; `side` may be nullptr (mono detector — all
    // bands apply to the mono channel regardless of their M/S target).
    void process (float* mid, float* side, int numSamples) noexcept
    {
        for (int b = 0; b < numScBands; ++b)
        {
            if (! bands[b].on)
                continue;

            const int chan = side == nullptr ? 0 : bands[b].chan;

            if (chan == 0 || chan == 1)
                runBand (b, 0, mid, numSamples);
            if (side != nullptr && (chan == 0 || chan == 2))
                runBand (b, 1, side, numSamples);
        }
    }

private:
    struct BiquadCoeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
    struct BiquadState { double z1 = 0.0, z2 = 0.0; };

    void runBand (int band, int chanIdx, float* data, int numSamples) noexcept
    {
        const int stages = numStages[(size_t) band];
        for (int s = 0; s < stages; ++s)
        {
            const auto& c = coeffs[(size_t) band][(size_t) s];
            auto& st = states[(size_t) band][(size_t) chanIdx][(size_t) s];

            for (int i = 0; i < numSamples; ++i)
            {
                const double x = (double) data[i];
                const double y = c.b0 * x + st.z1;
                st.z1 = c.b1 * x - c.a1 * y + st.z2;
                st.z2 = c.b2 * x - c.a2 * y;
                data[i] = (float) y;
            }
        }
    }

    void computeCoeffs (int band)
    {
        const auto& b = bands[(size_t) band];
        const double f = juce::jlimit (10.0, fs * 0.49, (double) b.freq);
        const double w0 = juce::MathConstants<double>::twoPi * f / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double q = juce::jmax (0.025, (double) b.q);
        const double A = std::pow (10.0, (double) b.gainDb / 40.0);

        auto& bandCoeffs = coeffs[(size_t) band];
        numStages[(size_t) band] = 1;

        auto set = [&] (double b0, double b1, double b2, double a0, double a1, double a2)
        {
            bandCoeffs[0] = { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
        };

        switch (b.type)
        {
            case ScFilterType::bell:
            {
                const double alpha = sw / (2.0 * q);
                set (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A,
                     1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
                break;
            }
            case ScFilterType::lowShelf:
            {
                const double alpha = sw / (2.0 * q);
                const double sq = 2.0 * std::sqrt (A) * alpha;
                set (A * ((A + 1.0) - (A - 1.0) * cw + sq),
                     2.0 * A * ((A - 1.0) - (A + 1.0) * cw),
                     A * ((A + 1.0) - (A - 1.0) * cw - sq),
                     (A + 1.0) + (A - 1.0) * cw + sq,
                     -2.0 * ((A - 1.0) + (A + 1.0) * cw),
                     (A + 1.0) + (A - 1.0) * cw - sq);
                break;
            }
            case ScFilterType::highShelf:
            {
                const double alpha = sw / (2.0 * q);
                const double sq = 2.0 * std::sqrt (A) * alpha;
                set (A * ((A + 1.0) + (A - 1.0) * cw + sq),
                     -2.0 * A * ((A - 1.0) + (A + 1.0) * cw),
                     A * ((A + 1.0) + (A - 1.0) * cw - sq),
                     (A + 1.0) - (A - 1.0) * cw + sq,
                     2.0 * ((A - 1.0) - (A + 1.0) * cw),
                     (A + 1.0) - (A - 1.0) * cw - sq);
                break;
            }
            case ScFilterType::lowCut:
            {
                const double alpha = sw / (2.0 * q);
                const double k = (1.0 + cw) * 0.5;
                set (k, -(1.0 + cw), k, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
                break;
            }
            case ScFilterType::highCut:
            {
                const double alpha = sw / (2.0 * q);
                const double k = (1.0 - cw) * 0.5;
                set (k, 1.0 - cw, k, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
                break;
            }
            case ScFilterType::lowCutBrickwall:
            case ScFilterType::highCutBrickwall:
            {
                // 16th-order Butterworth: 8 cascaded stages with the classic
                // pole-pair Q values.
                numStages[(size_t) band] = brickwallStages;
                for (int s = 0; s < brickwallStages; ++s)
                {
                    const double qk = 1.0 / (2.0 * std::sin ((2.0 * s + 1.0)
                                          * juce::MathConstants<double>::pi
                                          / (4.0 * brickwallStages)));
                    const double alpha = sw / (2.0 * qk);
                    double b0, b1, b2;
                    if (b.type == ScFilterType::lowCutBrickwall)
                    {
                        const double k = (1.0 + cw) * 0.5;
                        b0 = k; b1 = -(1.0 + cw); b2 = k;
                    }
                    else
                    {
                        const double k = (1.0 - cw) * 0.5;
                        b0 = k; b1 = 1.0 - cw; b2 = k;
                    }
                    const double a0 = 1.0 + alpha;
                    bandCoeffs[(size_t) s] = { b0 / a0, b1 / a0, b2 / a0,
                                               -2.0 * cw / a0, (1.0 - alpha) / a0 };
                }
                break;
            }
            case ScFilterType::notch:
            {
                const double alpha = sw / (2.0 * q);
                set (1.0, -2.0 * cw, 1.0, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
                break;
            }
            case ScFilterType::bandPass:
            {
                const double alpha = sw / (2.0 * q);
                set (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
                break;
            }
            case ScFilterType::allPass:
            {
                const double alpha = sw / (2.0 * q);
                set (1.0 - alpha, -2.0 * cw, 1.0 + alpha,
                     1.0 + alpha, -2.0 * cw, 1.0 - alpha);
                break;
            }
        }
    }

    double fs = 48000.0;
    std::array<Band, numScBands> bands;
    std::array<std::array<BiquadCoeffs, brickwallStages>, numScBands> coeffs;
    std::array<int, numScBands> numStages { 1, 1, 1, 1, 1, 1 };
    std::array<std::array<std::array<BiquadState, brickwallStages>, 2>, numScBands> states;
};

} // namespace mrdn
