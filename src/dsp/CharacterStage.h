#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Parameters.h"

namespace mrdn
{

// Analog-modelling character stage, routed pre- or post-compression and run
// inside the oversampled region (saturation is why the oversampler exists).
//
//   Clean  bit-transparent (skipped entirely)
//   Warm   asymmetric soft saturation — even + odd harmonics
//   Tape   symmetric tanh saturation — odd harmonics only
//   Drift  Warm plus a slow ±0.4 dB analog-style gain wobble
//
// Drive sets the level pushed into the shaper; small-signal gain is
// compensated so drive adds density, not loudness. Asymmetric modes are
// DC-blocked.
class CharacterStage
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        channels = juce::jlimit (1, 2, numChannels);
        dcCoeff = std::exp (-juce::MathConstants<double>::twoPi * 5.0 / fs);
        lfoInc = juce::MathConstants<double>::twoPi * driftRateHz / fs;
        reset();
    }

    void reset()
    {
        for (auto& s : dcState) s = 0.0;
        lfoPhase = 0.0;
    }

    void setParams (CharacterMode newMode, float driveDb)
    {
        mode = newMode;
        driveLin = juce::Decibels::decibelsToGain (driveDb);
        driveNorm = driveDb / maxDriveDb;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (mode == CharacterMode::clean)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float wobble = 1.0f;
            if (mode == CharacterMode::drift)
            {
                wobble = juce::Decibels::decibelsToGain (
                    driftDepthDb * driveNorm * (float) std::sin (lfoPhase));
                lfoPhase += lfoInc;
                if (lfoPhase > juce::MathConstants<double>::twoPi)
                    lfoPhase -= juce::MathConstants<double>::twoPi;
            }

            left[i] = shape (0, left[i] * wobble);
            if (right != nullptr)
                right[i] = shape (1, right[i] * wobble);
        }
    }

private:
    float shape (int ch, float x) noexcept
    {
        const float u = x * driveLin;
        float y;

        if (mode == CharacterMode::tape)
            return std::tanh (u) / driveLin;

        // Warm / Drift: asymmetric curve, then DC-block the result.
        y = std::tanh (u + 0.1f * u * u) / driveLin;

        auto& dc = dcState[(size_t) ch];
        const double hp = (double) y - dc;
        dc = dcCoeff * dc + (1.0 - dcCoeff) * (double) y;
        return (float) hp;
    }

    static constexpr float driftDepthDb = 0.4f;
    static constexpr double driftRateHz = 0.3;

    CharacterMode mode = CharacterMode::clean;
    double fs = 48000.0;
    int channels = 2;
    float driveLin = 1.0f, driveNorm = 0.0f;
    double dcCoeff = 0.0, lfoInc = 0.0, lfoPhase = 0.0;
    std::array<double, 2> dcState {};
};

} // namespace mrdn
