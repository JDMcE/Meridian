#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Parameters.h"

namespace mrdn
{

// Feed-forward compressor core.
//
// Per sample: level detection (peak or short-window RMS; stereo-linked via
// channel max, or fully independent per channel), a log-domain gain computer
// with a continuously variable knee (0–72 dB), then branching attack/release
// smoothing applied to the gain-reduction signal in dB (after Giannoulis
// et al., "Digital Dynamic Range Compressor Design").
//
// Program-dependent behaviour (v0.3):
//  - Dual-envelope release: GR is the average of a fast envelope (user
//    release) and a slow envelope (slow charge, 5x release discharge), so
//    brief peaks recover quickly while sustained material releases slowly.
//    Enabled by the Auto Release parameter or by style.
//  - Crest-modulated attack (Punch): the attack eases open on transient
//    (high crest factor) material so hits punch through before clamping.
//  - Two-stage optical release (Opto): fast out of deep reduction, sliding
//    to a long tail as GR approaches zero.
//  - HF-weighted detection (De-Ess): the detector hears highs ~13 dB louder
//    than lows, so sibilance drives the compression.
//  - Upward compression (Upward): levels *up* below the threshold, capped
//    at maxUpwardBoostDb.
//  - Auto Threshold: the threshold tracks a slow programme-loudness measure
//    (gated below -50 dB), making Threshold a level-independent offset —
//    aimed at dialog.
class CompressorEngine
{
public:
    struct Params
    {
        float thresholdDb = 0.0f;
        float ratio = 2.0f;
        float attackMs = 10.0f;
        float releaseMs = 100.0f;
        float kneeDb = 0.0f;
        float makeupDb = 0.0f;
        DetectorMode detector = DetectorMode::peak;
        Style style = Style::versatile;
        bool autoRelease = false;
        bool autoThreshold = false;

        // Stereo behaviour: link amount blends each channel's detector with
        // the channel max (1 = fully linked). msTarget selects M/S modes:
        // -1 = normal stereo, 0 = compress ch0 only, 1 = compress ch1 only
        // (the other channel passes through its lookahead delay dry).
        float linkAmount01 = 1.0f;
        int msTarget = -1;

        // v0.4 gain-shaping utilities.
        float mixPercent = 100.0f;  // <100: parallel blend; >100: GR scaling
        float rangeDb = maxRangeDb; // caps |applied GR|
        float holdMs = 0.0f;        // freezes GR before release begins
        float lookaheadMs = 0.0f;   // audio delay so the envelope leads
    };

    // How a style remaps the user parameters. Scales apply to the user's
    // values; floors/caps then clamp the results.
    struct StyleTraits
    {
        bool forceDetector = false;
        DetectorMode detector = DetectorMode::peak;
        float rmsWindowMs = 10.0f;
        float attackScale = 1.0f;
        float releaseScale = 1.0f;
        float kneeFloorDb = 0.0f;
        float kneeCapDb = maxKneeDb;
        float ratioScale = 1.0f;
        float ratioFloor = minRatio;
        float ratioCap = 1000.0f;   // > maxRatio means uncapped
        bool hardRatio = false;     // effectively infinite ratio (Limit)
        bool upward = false;
        bool dualEnvelope = false;  // program-dependent release built in
        bool twoStageRelease = false;
        bool crestAttack = false;
        bool hfWeighted = false;
    };

    static StyleTraits styleTraits (Style s)
    {
        StyleTraits t;
        switch (s)
        {
            case Style::versatile: break;
            case Style::smooth:    t = { true, DetectorMode::rms, 20.0f, 2.0f, 1.5f, 6.0f, maxKneeDb,
                                         1.0f, minRatio, 1000.0f, false, false, true, false, false, false }; break;
            case Style::variMu:    t = { true, DetectorMode::rms, 15.0f, 3.0f, 2.0f, 18.0f, maxKneeDb,
                                         1.25f, minRatio, 1000.0f, false, false, true, false, false, false }; break;
            case Style::opto:      t = { true, DetectorMode::rms, 15.0f, 2.0f, 1.0f, 12.0f, maxKneeDb,
                                         1.0f, minRatio, 1000.0f, false, false, false, true, false, false }; break;
            case Style::punch:     t = { true, DetectorMode::peak, 10.0f, 0.5f, 0.6f, 0.0f, 6.0f,
                                         1.0f, minRatio, 1000.0f, false, false, false, false, true, false }; break;
            case Style::upward:    t = { true, DetectorMode::rms, 20.0f, 2.0f, 1.5f, 0.0f, maxKneeDb,
                                         1.0f, minRatio, 1000.0f, false, true, true, false, false, false }; break;
            case Style::maximize:  t = { true, DetectorMode::peak, 10.0f, 0.2f, 0.5f, 0.0f, 6.0f,
                                         1.0f, 10.0f, 1000.0f, false, false, true, false, false, false }; break;
            case Style::bus:       t = { true, DetectorMode::rms, 15.0f, 1.5f, 1.2f, 4.0f, maxKneeDb,
                                         1.0f, minRatio, 1000.0f, false, false, true, false, false, false }; break;
            case Style::mastering: t = { true, DetectorMode::rms, 30.0f, 2.0f, 2.0f, 12.0f, maxKneeDb,
                                         1.0f, minRatio, 4.0f, false, false, true, false, false, false }; break;
            case Style::pump:      t = { true, DetectorMode::peak, 10.0f, 0.7f, 0.3f, 0.0f, 0.0f,
                                         1.5f, minRatio, 1000.0f, false, false, false, false, false, false }; break;
            case Style::deEss:     t = { true, DetectorMode::rms, 5.0f, 0.5f, 0.7f, 6.0f, maxKneeDb,
                                         1.0f, minRatio, 1000.0f, false, false, false, false, false, true }; break;
            case Style::limit:     t = { true, DetectorMode::peak, 10.0f, 0.1f, 0.5f, 0.0f, 3.0f,
                                         1.0f, minRatio, 1000.0f, true, false, true, false, false, false }; break;
            case Style::vocal:     t = { true, DetectorMode::rms, 15.0f, 0.8f, 1.0f, 9.0f, maxKneeDb,
                                         1.0f, minRatio, 1000.0f, false, false, true, false, false, false }; break;
            case Style::digital:   t = { true, DetectorMode::peak, 10.0f, 1.0f, 1.0f, 0.0f, 0.0f,
                                         1.0f, minRatio, 1000.0f, false, false, false, false, false, false }; break;
            default: break;
        }
        return t;
    }

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        channels = juce::jlimit (1, 2, numChannels);
        // Detector pre-emphasis highpass (~3 kHz one-pole) for De-Ess.
        hpCoeff = std::exp (-juce::MathConstants<double>::twoPi * 3000.0 / fs);

        const int maxDelay = (int) std::ceil (fs * 0.001 * maxLookaheadMs) + 1;
        for (auto& d : delayLines)
        {
            d.buffer.assign ((size_t) maxDelay, 0.0f);
            d.writeIndex = 0;
        }

        // Per-sample smoothing (~5 ms) of the direct-gain parameters, so
        // block-rate parameter updates can't step audibly.
        gainSmoothCoeff = 1.0f - std::exp (-1.0f / (float) (fs * 0.005));

        reset();
        setParams (params);
    }

    void reset()
    {
        for (auto& s : grFastDb) s = 0.0f;
        for (auto& s : grSlowDb) s = 0.0f;
        for (auto& s : meanSquare) s = 0.0f;
        for (auto& s : hpState) s = 0.0f;
        for (auto& s : crestPeak) s = 0.0f;
        for (auto& s : crestRms) s = 0.0f;
        for (auto& s : loudnessDb) s = -50.0f;
        for (auto& s : appliedGrDb) s = 0.0f;
        for (auto& h : holdCounter) h = 0;
        for (auto& d : delayLines)
            std::fill (d.buffer.begin(), d.buffer.end(), 0.0f);
        snapSmoothedGains = true;
    }

    void setParams (const Params& p)
    {
        params = p;
        traits = styleTraits (p.style);

        effDetector = traits.forceDetector ? traits.detector : p.detector;
        effKneeDb = juce::jlimit (traits.kneeFloorDb, traits.kneeCapDb, p.kneeDb);
        effRatio = traits.hardRatio
                       ? 1000.0f
                       : juce::jlimit (traits.ratioFloor, traits.ratioCap,
                                       p.ratio * traits.ratioScale);
        dualEnvelope = traits.dualEnvelope || p.autoRelease;

        const float attackMs = juce::jmax (0.02f, p.attackMs * traits.attackScale);
        const float releaseMs = p.releaseMs * traits.releaseScale;

        attackCoeff = timeToCoeff (attackMs);
        attackSlowCoeff = timeToCoeff (attackMs * 3.0f); // crest-eased attack
        releaseCoeff = timeToCoeff (releaseMs);
        releaseCoeffFast = timeToCoeff (releaseMs * 0.3f); // opto endpoints
        releaseCoeffSlow = timeToCoeff (releaseMs * 3.0f);

        // Slow program envelope: charges over >=200 ms, discharges at 5x
        // the user release.
        slowAttackCoeff = timeToCoeff (juce::jmax (200.0f, attackMs * 4.0f));
        slowReleaseCoeff = timeToCoeff (releaseMs * 5.0f);

        rmsCoeff = timeToCoeff (traits.rmsWindowMs);
        crestPeakCoeff = timeToCoeff (2.0f);
        crestRmsCoeff = timeToCoeff (60.0f);
        loudnessCoeff = timeToCoeff (1500.0f); // auto-threshold tracker

        makeupLin = juce::Decibels::decibelsToGain (p.makeupDb);

        // Mix: amplitude blend up to 100 %, GR scaling (over-compression)
        // beyond — continuous at 100 %.
        const float mix = p.mixPercent * 0.01f;
        wetAmount = juce::jmin (mix, 1.0f);
        grScale = juce::jmax (mix, 1.0f);

        holdSamples = (int) std::lround (fs * 0.001 * (double) p.holdMs);
        delaySamples = juce::jlimit (0, juce::jmax (0, (int) delayLines[0].buffer.size() - 1),
                                     (int) std::lround (fs * 0.001 * (double) p.lookaheadMs));

        if (snapSmoothedGains)
        {
            snapSmoothedGains = false;
            makeupCur = makeupLin;
            wetCur = wetAmount;
            grScaleCur = grScale;
            rangeCur = p.rangeDb;
        }
    }

    int getLookaheadSamples() const noexcept { return delaySamples; }

    // Static transfer curve with the style's *effective* knee/ratio and the
    // given threshold: input level in dB -> output level in dB. Shared by
    // the realtime path, the tests, and the (future) knee display.
    float computeOutputLevelDb (float inDb, float thresholdDb) const noexcept
    {
        if (traits.upward)
        {
            if (inDb >= thresholdDb)
                return inDb;
            const float boost = juce::jmin ((thresholdDb - inDb) * (1.0f - 1.0f / effRatio),
                                            maxUpwardBoostDb);
            return inDb + boost;
        }

        const float W = effKneeDb;
        const float invR = 1.0f / effRatio;
        const float twiceOver = 2.0f * (inDb - thresholdDb);

        if (twiceOver < -W)
            return inDb;

        if (twiceOver <= W && W > 0.0f)
        {
            const float x = inDb - thresholdDb + W * 0.5f;
            return inDb + (invR - 1.0f) * x * x / (2.0f * W);
        }

        return thresholdDb + (inDb - thresholdDb) * invR;
    }

    float computeOutputLevelDb (float inDb) const noexcept
    {
        return computeOutputLevelDb (inDb, params.thresholdDb);
    }

    // Linear gain the detector applies to a static (DC-like) signal before
    // level measurement — 1 except for HF-weighted styles. Exposed so tests
    // can predict static curves for De-Ess.
    float detectorDcGain() const noexcept { return traits.hfWeighted ? deEssLowGain : 1.0f; }

    // Processes in place. `right` may be nullptr for mono. The detector
    // signal is separate from the audio (external sidechain / sidechain EQ /
    // M-S routing); pass the audio pointers again for classic internal
    // detection. The wet gain is computed from the undelayed detector and
    // applied to the (lookahead-)delayed audio, blended per the mix setting.
    // Returns the maximum |applied gain change| over the block in dB for
    // metering (upward compression reports its boost magnitude).
    float process (float* left, float* right,
                   const float* detLeft, const float* detRight, int numSamples) noexcept
    {
        float maxGrDb = 0.0f;

        if (right == nullptr)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                smoothGainParams();
                const float gain = mixedGain (advanceEnvelope (0, detectSample (0, detLeft[i])));
                left[i] = delayLines[0].push (left[i], delaySamples) * gain;
                maxGrDb = juce::jmax (maxGrDb, std::abs (appliedGrDb[0]));
            }
        }
        else if (params.msTarget >= 0)
        {
            // M/S mode: one channel compressed from a single detector, the
            // other delayed dry so the pair stays time-aligned.
            const int t = params.msTarget;
            for (int i = 0; i < numSamples; ++i)
            {
                smoothGainParams();
                const float gain = mixedGain (advanceEnvelope (0, detectSample (0, detLeft[i])));
                const float y0 = delayLines[0].push (left[i], delaySamples);
                const float y1 = delayLines[1].push (right[i], delaySamples);
                left[i] = t == 0 ? y0 * gain : y0;
                right[i] = t == 1 ? y1 * gain : y1;
                maxGrDb = juce::jmax (maxGrDb, std::abs (appliedGrDb[0]));
            }
        }
        else
        {
            const float amt = params.linkAmount01;
            for (int i = 0; i < numSamples; ++i)
            {
                smoothGainParams();
                const float d0 = detectSample (0, detLeft[i]);
                const float d1 = detectSample (1, detRight[i]);
                const float m = juce::jmax (d0, d1);
                const float e0 = amt * m + (1.0f - amt) * d0;
                const float e1 = amt * m + (1.0f - amt) * d1;

                left[i] = delayLines[0].push (left[i], delaySamples)
                          * mixedGain (advanceEnvelope (0, e0));
                right[i] = delayLines[1].push (right[i], delaySamples)
                           * mixedGain (advanceEnvelope (1, e1));
                maxGrDb = juce::jmax (maxGrDb,
                    juce::jmax (std::abs (appliedGrDb[0]), std::abs (appliedGrDb[1])));
            }
        }

        return maxGrDb;
    }

    float currentGainReductionDb() const noexcept
    {
        return juce::jmax (std::abs (appliedGrDb[0]), std::abs (appliedGrDb[1]));
    }

private:
    struct DelayLine
    {
        std::vector<float> buffer;
        int writeIndex = 0;

        float push (float x, int delay) noexcept
        {
            if (delay <= 0 || buffer.empty())
                return x;
            buffer[(size_t) writeIndex] = x;
            int read = writeIndex - delay;
            if (read < 0)
                read += (int) buffer.size();
            const float y = buffer[(size_t) read];
            if (++writeIndex >= (int) buffer.size())
                writeIndex = 0;
            return y;
        }
    };

    float currentGr (int idx) const noexcept
    {
        const auto i = (size_t) idx;
        return dualEnvelope ? 0.5f * (grFastDb[i] + grSlowDb[i]) : grFastDb[i];
    }

    // Wet/dry blend of a wet gain, per the mix setting (wet gain already
    // includes makeup).
    float mixedGain (float wetGain) const noexcept
    {
        return (1.0f - wetCur) + wetCur * wetGain;
    }

    // Per-sample ramping of the direct-gain parameters toward their
    // block-rate targets (called once per sample frame).
    void smoothGainParams() noexcept
    {
        makeupCur += (makeupLin - makeupCur) * gainSmoothCoeff;
        wetCur += (wetAmount - wetCur) * gainSmoothCoeff;
        grScaleCur += (grScale - grScaleCur) * gainSmoothCoeff;
        rangeCur += (params.rangeDb - rangeCur) * gainSmoothCoeff;
    }

    // Updates the GR envelopes for one channel from a detector level and
    // returns the linear *wet* gain to apply (mix scaling of GR and the
    // range cap included, makeup included). GR is positive for downward
    // compression, negative for upward boost.
    float advanceEnvelope (int idx, float det) noexcept
    {
        const auto i = (size_t) idx;
        const float detDb = juce::Decibels::gainToDecibels (det, -120.0f);

        float thresholdDb = params.thresholdDb;
        if (params.autoThreshold)
        {
            // Track programme loudness, gated so silence doesn't drag it down.
            if (detDb > -50.0f)
                loudnessDb[i] = loudnessCoeff * loudnessDb[i]
                                + (1.0f - loudnessCoeff) * detDb;
            thresholdDb = juce::jlimit (-120.0f, 0.0f, loudnessDb[i] + params.thresholdDb);
        }

        const float targetGrDb = detDb - computeOutputLevelDb (detDb, thresholdDb);

        // Crest-modulated attack: ease open on transient material.
        float attCoeff = attackCoeff;
        if (traits.crestAttack)
        {
            const float crest01 = juce::jlimit (0.0f, 1.0f,
                (crestPeak[i] / juce::jmax (crestRms[i], 1.0e-9f) - 1.2f) * (1.0f / 1.5f));
            attCoeff = attackCoeff + (attackSlowCoeff - attackCoeff) * crest01;
        }

        // Fast envelope (with hold, and the optical two-stage release when
        // active). Hold freezes the envelope between attack and release.
        {
            auto& gr = grFastDb[i];
            if (std::abs (targetGrDb) > std::abs (gr))
            {
                holdCounter[i] = holdSamples;
                gr = attCoeff * gr + (1.0f - attCoeff) * targetGrDb;
            }
            else if (holdCounter[i] > 0)
            {
                --holdCounter[i];
            }
            else
            {
                float coeff;
                if (traits.twoStageRelease)
                {
                    const float t = juce::jmin (std::abs (gr) * (1.0f / 12.0f), 1.0f);
                    coeff = releaseCoeffSlow + (releaseCoeffFast - releaseCoeffSlow) * t;
                }
                else
                {
                    coeff = releaseCoeff;
                }
                gr = coeff * gr + (1.0f - coeff) * targetGrDb;
            }
        }

        if (dualEnvelope)
        {
            auto& gr = grSlowDb[i];
            const float coeff = std::abs (targetGrDb) > std::abs (gr) ? slowAttackCoeff
                                                                      : slowReleaseCoeff;
            gr = coeff * gr + (1.0f - coeff) * targetGrDb;
        }

        // Over-compression scaling (mix > 100 %) and the range cap, then
        // makeup — this is the gain the wet path actually receives.
        const float applied = juce::jlimit (-rangeCur, rangeCur,
                                            currentGr (idx) * grScaleCur);
        appliedGrDb[i] = applied;
        return juce::Decibels::decibelsToGain (-applied) * makeupCur;
    }

    float detectSample (int ch, float x) noexcept
    {
        const auto i = (size_t) ch;

        if (traits.hfWeighted)
        {
            // One-pole highpass; the detector hears lows at deEssLowGain and
            // highs boosted, so sibilance dominates the level estimate.
            const float lp = (float) (hpState[i] = hpCoeff * hpState[i]
                                                   + (1.0 - hpCoeff) * (double) x);
            x = deEssLowGain * x + deEssHighGain * (x - lp);
        }

        float level;
        if (effDetector == DetectorMode::peak)
        {
            level = std::abs (x);
        }
        else
        {
            auto& ms = meanSquare[i];
            ms = rmsCoeff * ms + (1.0f - rmsCoeff) * x * x;
            level = std::sqrt (ms);
        }

        if (traits.crestAttack)
        {
            const float a = std::abs (x);
            crestPeak[i] = a > crestPeak[i]
                               ? a
                               : crestPeakCoeff * crestPeak[i] + (1.0f - crestPeakCoeff) * a;
            crestRms[i] = crestRmsCoeff * crestRms[i] + (1.0f - crestRmsCoeff) * a;
        }

        return level;
    }

    float timeToCoeff (float ms) const noexcept
    {
        return std::exp (-1.0f / (float) (fs * 0.001 * (double) ms));
    }

    static constexpr float deEssLowGain = 0.3f, deEssHighGain = 1.4f;

    Params params;
    StyleTraits traits;
    double fs = 48000.0;
    int channels = 2;

    float effKneeDb = 0.0f, effRatio = 2.0f;
    DetectorMode effDetector = DetectorMode::peak;
    bool dualEnvelope = false;

    float attackCoeff = 0.0f, attackSlowCoeff = 0.0f;
    float releaseCoeff = 0.0f, releaseCoeffFast = 0.0f, releaseCoeffSlow = 0.0f;
    float slowAttackCoeff = 0.0f, slowReleaseCoeff = 0.0f;
    float rmsCoeff = 0.0f, crestPeakCoeff = 0.0f, crestRmsCoeff = 0.0f;
    float loudnessCoeff = 0.0f;
    float makeupLin = 1.0f;
    float wetAmount = 1.0f, grScale = 1.0f;
    float makeupCur = 1.0f, wetCur = 1.0f, grScaleCur = 1.0f, rangeCur = maxRangeDb;
    float gainSmoothCoeff = 1.0f;
    bool snapSmoothedGains = true;
    int holdSamples = 0, delaySamples = 0;
    double hpCoeff = 0.0;

    std::array<float, 2> grFastDb {}, grSlowDb {}, meanSquare {};
    std::array<float, 2> crestPeak {}, crestRms {}, loudnessDb { -50.0f, -50.0f };
    std::array<float, 2> appliedGrDb {};
    std::array<int, 2> holdCounter {};
    std::array<double, 2> hpState {};
    std::array<DelayLine, 2> delayLines;
};

} // namespace mrdn
