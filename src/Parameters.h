#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mrdn
{

// Detector level estimation modes. Order must match detectorChoices.
enum class DetectorMode
{
    peak = 0,
    rms
};

inline const juce::StringArray detectorChoices { "Peak", "RMS" };

// Compression styles. Order must match styleChoices (append-only to keep
// saved states stable). Each style maps the user parameters onto effective
// detector/knee/ballistics settings and behaviour flags — see
// CompressorEngine::styleTraits.
enum class Style
{
    versatile = 0,  // clean all-rounder (default)
    smooth,         // gentle RMS levelling, program-dependent release
    variMu,         // tube-style: slow, wide knee, ratio grows
    opto,           // optical: two-stage GR-dependent release
    punch,          // transient-forward: attack eases open on peaks
    upward,         // upward compression below threshold
    maximize,       // to-the-max loudness slam
    bus,            // mix-bus glue
    mastering,      // transparent, slow, low-ratio precision
    pump,           // aggressive fast-release pumping
    deEss,          // HF-weighted detection for sibilance
    limit,          // hard limiting, effectively infinite ratio
    vocal,          // dialog/vocal-tuned levelling
    digital,        // classic hard-knee digital, exactly the user's numbers
    numStyles
};

inline const juce::StringArray styleChoices {
    "Versatile", "Smooth", "Vari-Mu", "Opto", "Punch", "Upward", "Maximize",
    "Bus", "Mastering", "Pump", "De-Ess", "Limit", "Vocal", "Digital"
};

// Oversampling factors. Order must match oversamplingChoices.
inline const juce::StringArray oversamplingChoices { "Off", "2x", "4x", "8x", "16x", "32x" };
constexpr int oversamplingFactors[] = { 1, 2, 4, 8, 16, 32 };

// Character/analog-modelling modes. Order must match characterChoices.
enum class CharacterMode
{
    clean = 0,  // bit-transparent
    warm,       // asymmetric soft saturation (even + odd harmonics)
    tape,       // symmetric tanh saturation (odd harmonics)
    drift       // warm + slow analog-style gain wobble
};

inline const juce::StringArray characterChoices { "Clean", "Warm", "Tape", "Drift" };
inline const juce::StringArray characterPosChoices { "Pre", "Post" };

// Stereo linking / M-S processing modes. Order must match linkModeChoices.
enum class LinkMode
{
    stereo = 0,   // L/R processing, variable link amount
    midOnly,      // compress mid, side passes (delayed) dry
    sideOnly,     // compress side, mid passes dry
    midToSide,    // detect mid, compress side
    sideToMid     // detect side, compress mid
};

inline const juce::StringArray linkModeChoices {
    "Stereo", "Mid Only", "Side Only", "Mid to Side", "Side to Mid"
};

inline const juce::StringArray scSourceChoices { "Internal", "External" };

// Detector trigger source (v0.7). Order must match triggerModeChoices.
// Audio = normal detection; Host Sync = a tempo-locked pulse train drives
// the detector; MIDI = incoming notes gate the detector. In both trigger
// modes the pulse is 0 dBFS, so Threshold sets the ducking depth and
// Attack/Release shape it.
enum class TriggerMode
{
    audio = 0,
    hostSync,
    midi
};

inline const juce::StringArray triggerModeChoices { "Audio", "Host Sync", "MIDI" };

// Host-sync divisions in quarter notes. Order must match syncRateChoices.
inline const juce::StringArray syncRateChoices {
    "1/1", "1/2", "1/2T", "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T", "1/16", "1/16T"
};
constexpr double syncRateQuarters[] = {
    4.0, 2.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 1.0 / 6.0
};

// Sidechain EQ ------------------------------------------------------------

constexpr int numScBands = 6;

// Order must match scTypeChoices. Brickwall = 16th-order Butterworth.
enum class ScFilterType
{
    bell = 0,
    lowShelf,
    highShelf,
    lowCut,
    highCut,
    lowCutBrickwall,
    highCutBrickwall,
    notch,
    bandPass,
    allPass
};

inline const juce::StringArray scTypeChoices {
    "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
    "Low Cut Brick", "High Cut Brick", "Notch", "Band Pass", "All Pass"
};

inline const juce::StringArray scChanChoices { "Both", "Mid", "Side" };

constexpr float minScFreq = 20.0f, maxScFreq = 20000.0f;
constexpr float maxScGainDb = 24.0f;
constexpr float minScQ = 0.1f, maxScQ = 18.0f;

inline juce::String scBandParamId (int band, const char* suffix)
{
    return "sc" + juce::String (band) + "_" + suffix;
}

// Parameter ranges ---------------------------------------------------------

constexpr float minThresholdDb = -60.0f, maxThresholdDb = 0.0f;
constexpr float minRatio = 1.0f, maxRatio = 20.0f;
constexpr float minAttackMs = 0.02f, maxAttackMs = 250.0f;  // sub-ms capable
constexpr float minReleaseMs = 10.0f, maxReleaseMs = 2500.0f;
constexpr float maxKneeDb = 72.0f;
constexpr float maxMakeupDb = 24.0f;
constexpr float maxTrimDb = 24.0f;
constexpr float maxUpwardBoostDb = 12.0f;  // style-level cap for Upward (Range caps further)
constexpr float maxDriveDb = 24.0f;
constexpr float maxMixPercent = 200.0f;
constexpr float maxRangeDb = 60.0f;
constexpr float maxHoldMs = 500.0f;
constexpr float maxLookaheadMs = 20.0f;

// Parameter IDs ------------------------------------------------------------

namespace id
{
    inline const juce::String threshold { "threshold" };
    inline const juce::String ratio     { "ratio" };
    inline const juce::String attack    { "attack" };
    inline const juce::String release   { "release" };
    inline const juce::String knee      { "knee" };
    inline const juce::String makeup    { "makeup" };
    inline const juce::String inTrim    { "in_trim" };
    inline const juce::String outTrim   { "out_trim" };
    inline const juce::String detector  { "detector" };
    inline const juce::String style     { "style" };
    inline const juce::String linkAmount { "link_amount" };
    inline const juce::String linkMode   { "link_mode" };
    inline const juce::String scSource   { "sc_source" };
    inline const juce::String audition   { "audition" };
    inline const juce::String bypass    { "bypass" };
    inline const juce::String autoRelease   { "auto_release" };
    inline const juce::String autoThreshold { "auto_threshold" };
    inline const juce::String oversampling  { "oversampling" };
    inline const juce::String character    { "character" };
    inline const juce::String characterPos { "character_pos" };
    inline const juce::String drive        { "drive" };
    inline const juce::String mix          { "mix" };
    inline const juce::String range        { "range" };
    inline const juce::String hold         { "hold" };
    inline const juce::String lookahead    { "lookahead" };
    inline const juce::String autoGain     { "auto_gain" };
    inline const juce::String triggerMode  { "trigger_mode" };
    inline const juce::String syncRate     { "sync_rate" };
    inline const juce::String syncOffset   { "sync_offset" };
    inline const juce::String pulseLength  { "pulse_length" };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto dbAttr = juce::AudioParameterFloatAttributes().withLabel ("dB");
    auto msAttr = juce::AudioParameterFloatAttributes().withLabel ("ms");

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::threshold, 1), "Threshold",
        juce::NormalisableRange<float> (minThresholdDb, maxThresholdDb, 0.01f), 0.0f, dbAttr));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::ratio, 1), "Ratio",
        juce::NormalisableRange<float> (minRatio, maxRatio, 0.01f, 0.4f), 2.0f,
        juce::AudioParameterFloatAttributes().withLabel (":1")));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::attack, 1), "Attack",
        juce::NormalisableRange<float> (minAttackMs, maxAttackMs, 0.001f, 0.3f), 10.0f, msAttr));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::release, 1), "Release",
        juce::NormalisableRange<float> (minReleaseMs, maxReleaseMs, 0.1f, 0.3f), 100.0f, msAttr));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::knee, 1), "Knee",
        juce::NormalisableRange<float> (0.0f, maxKneeDb, 0.01f), 0.0f, dbAttr));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::makeup, 1), "Makeup Gain",
        juce::NormalisableRange<float> (-maxMakeupDb, maxMakeupDb, 0.01f), 0.0f, dbAttr));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::inTrim, 1), "Input Trim",
        juce::NormalisableRange<float> (-maxTrimDb, maxTrimDb, 0.01f), 0.0f, dbAttr));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::outTrim, 1), "Output Trim",
        juce::NormalisableRange<float> (-maxTrimDb, maxTrimDb, 0.01f), 0.0f, dbAttr));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (id::detector, 1), "Detector", detectorChoices,
        (int) DetectorMode::peak));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (id::style, 1), "Style", styleChoices, (int) Style::versatile));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::linkAmount, 1), "Stereo Link",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (id::linkMode, 1), "Link Mode", linkModeChoices,
        (int) LinkMode::stereo));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (id::scSource, 1), "Sidechain Source", scSourceChoices, 0));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID (id::audition, 1), "Audition Sidechain", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID (id::autoRelease, 1), "Auto Release", false));

    // With Auto Threshold on, the Threshold knob becomes an offset relative
    // to the tracked programme loudness (level-independent compression).
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID (id::autoThreshold, 1), "Auto Threshold", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (id::oversampling, 1), "Oversampling",
        oversamplingChoices, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (id::character, 1), "Character", characterChoices,
        (int) CharacterMode::clean));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (id::characterPos, 1), "Character Position",
        characterPosChoices, 1));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::drive, 1), "Drive",
        juce::NormalisableRange<float> (0.0f, maxDriveDb, 0.01f), 0.0f, dbAttr));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::mix, 1), "Mix",
        juce::NormalisableRange<float> (0.0f, maxMixPercent, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::range, 1), "Range",
        juce::NormalisableRange<float> (0.0f, maxRangeDb, 0.01f), maxRangeDb, dbAttr));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::hold, 1), "Hold",
        juce::NormalisableRange<float> (0.0f, maxHoldMs, 0.1f, 0.5f), 0.0f, msAttr));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::lookahead, 1), "Lookahead",
        juce::NormalisableRange<float> (0.0f, maxLookaheadMs, 0.01f), 0.0f, msAttr));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID (id::autoGain, 1), "Auto Gain", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID (id::bypass, 1), "Bypass", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (id::triggerMode, 1), "Trigger", triggerModeChoices,
        (int) TriggerMode::audio));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (id::syncRate, 1), "Sync Rate", syncRateChoices, 3)); // 1/4

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::syncOffset, 1), "Sync Offset",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<P> (
        juce::ParameterID (id::pulseLength, 1), "Pulse Length",
        juce::NormalisableRange<float> (1.0f, 100.0f, 0.1f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    for (int b = 0; b < numScBands; ++b)
    {
        const auto bandName = "SC " + juce::String (b + 1) + " ";

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID (scBandParamId (b, "on"), 1), bandName + "On", false));

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (scBandParamId (b, "type"), 1), bandName + "Type",
            scTypeChoices, (int) ScFilterType::bell));

        params.push_back (std::make_unique<P> (
            juce::ParameterID (scBandParamId (b, "freq"), 1), bandName + "Freq",
            juce::NormalisableRange<float> (minScFreq, maxScFreq, 0.1f, 0.25f), 1000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        params.push_back (std::make_unique<P> (
            juce::ParameterID (scBandParamId (b, "gain"), 1), bandName + "Gain",
            juce::NormalisableRange<float> (-maxScGainDb, maxScGainDb, 0.01f), 0.0f, dbAttr));

        params.push_back (std::make_unique<P> (
            juce::ParameterID (scBandParamId (b, "q"), 1), bandName + "Q",
            juce::NormalisableRange<float> (minScQ, maxScQ, 0.001f, 0.3f), 0.71f));

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (scBandParamId (b, "chan"), 1), bandName + "Channel",
            scChanChoices, 0));
    }

    return { params.begin(), params.end() };
}

} // namespace mrdn
