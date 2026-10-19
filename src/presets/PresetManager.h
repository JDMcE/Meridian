#pragma once

#include <jdm/presets/PresetManager.h>
#include "../Parameters.h"

namespace mrdn
{

using jdm::PresetManager;
using jdm::PresetInfo;

// Meridian's preset ecosystem configuration: file format identity plus the
// factory library, handed to the shared jdm::PresetManager. Presets without
// a MIDIMAP child inherit the session's current mappings on load (loading a
// sound shouldn't silently unplug the controller). State schema mirrors
// MeridianAudioProcessor::stateVersion.
inline jdm::PresetProductConfig makePresetConfig()
{
    jdm::PresetProductConfig c;
    c.productName = "Meridian";
    c.extension = "mrdn";
    c.presetTag = juce::Identifier ("MeridianPreset");
    c.stateTag = juce::Identifier ("MeridianState");
    c.stateVersion = 2;
    c.inheritChildTrees = { "MIDIMAP" };

    c.factory = {
        { "Init", "Basics", "default;clean",
          "Clean starting point: Versatile style at a gentle 2:1.",
          {} },

        { "Vocal Rider", "Vocals", "vocal;lead;smooth",
          "Steady 3:1 vocal levelling with a soft knee and programme-dependent release.",
          { { id::style, (float) Style::vocal }, { id::threshold, -24.0f },
            { id::ratio, 3.0f }, { id::attack, 8.0f }, { id::release, 120.0f },
            { id::knee, 12.0f }, { id::autoRelease, 1.0f }, { id::makeup, 3.0f } } },

        { "Dialog Leveler", "Vocals", "dialog;post;auto",
          "Auto Threshold tracks the programme, so dialog sits at the same level "
          "regardless of how hot it comes in.",
          { { id::style, (float) Style::vocal }, { id::autoThreshold, 1.0f },
            { id::threshold, -8.0f }, { id::ratio, 2.5f }, { id::attack, 15.0f },
            { id::autoRelease, 1.0f }, { id::knee, 15.0f } } },

        { "De-Ess Tamer", "Vocals", "vocal;sibilance;de-ess",
          "HF-weighted detection clamps sibilance without dulling the body.",
          { { id::style, (float) Style::deEss }, { id::threshold, -30.0f },
            { id::ratio, 4.0f }, { id::attack, 0.5f }, { id::release, 60.0f },
            { id::knee, 6.0f } } },

        { "Drum Bus Punch", "Drums", "drums;bus;punch",
          "Slow attack lets transients through, then grabs the sustain: punch, not squash.",
          { { id::style, (float) Style::punch }, { id::threshold, -18.0f },
            { id::ratio, 4.0f }, { id::attack, 25.0f }, { id::release, 90.0f },
            { id::knee, 4.0f } } },

        { "Parallel Drum Crush", "Drums", "drums;parallel;aggressive",
          "Pumping over-compression blended in parallel at 55% for weight without losing hits.",
          { { id::style, (float) Style::pump }, { id::threshold, -35.0f },
            { id::ratio, 10.0f }, { id::attack, 1.0f }, { id::release, 50.0f },
            { id::mix, 55.0f }, { id::makeup, 6.0f } } },

        { "Opto Bass", "Instruments", "bass;opto;smooth",
          "Two-stage optical release keeps bass thick and even.",
          { { id::style, (float) Style::opto }, { id::threshold, -22.0f },
            { id::ratio, 5.0f }, { id::attack, 12.0f }, { id::release, 200.0f },
            { id::knee, 10.0f }, { id::makeup, 2.0f } } },

        { "Tape Warmth", "Color", "tape;saturation;vari-mu",
          "Vari-Mu compression into Tape saturation inside the oversampled path.",
          { { id::style, (float) Style::variMu }, { id::threshold, -18.0f },
            { id::ratio, 2.5f }, { id::knee, 20.0f },
            { id::character, (float) CharacterMode::tape }, { id::drive, 8.0f },
            { id::oversampling, 1.0f } } },

        { "Mix Bus Glue", "Bus & Master", "bus;glue;mix",
          "The classic 2:1 slow-attack bus treatment - a dB or two of gel, nothing more.",
          { { id::style, (float) Style::bus }, { id::threshold, -20.0f },
            { id::ratio, 2.0f }, { id::attack, 30.0f }, { id::autoRelease, 1.0f },
            { id::knee, 18.0f }, { id::makeup, 1.5f } } },

        { "Mastering Touch", "Bus & Master", "mastering;transparent",
          "Transparent low-ratio precision with a wide knee, lookahead, and 4x oversampling.",
          { { id::style, (float) Style::mastering }, { id::threshold, -16.0f },
            { id::ratio, 1.5f }, { id::attack, 50.0f }, { id::release, 300.0f },
            { id::knee, 24.0f }, { id::lookahead, 3.0f },
            { id::oversampling, 2.0f } } },

        { "Loudness Maximize", "Bus & Master", "loud;maximize;master",
          "To-the-max slam: fast, deep, and oversampled. Watch the meters.",
          { { id::style, (float) Style::maximize }, { id::threshold, -12.0f },
            { id::ratio, 8.0f }, { id::attack, 0.5f }, { id::release, 80.0f },
            { id::lookahead, 5.0f }, { id::oversampling, 2.0f },
            { id::makeup, 4.0f } } },

        { "Brickwall Safety", "Bus & Master", "limiter;safety;ceiling",
          "Hard limiting with 10 ms lookahead and 8x oversampling as a peak backstop.",
          { { id::style, (float) Style::limit }, { id::threshold, -3.0f },
            { id::ratio, 20.0f }, { id::attack, 0.02f }, { id::release, 50.0f },
            { id::lookahead, 10.0f }, { id::oversampling, 3.0f } } },

        { "Upward Lift", "Effects", "upward;detail;quiet",
          "Upward compression raises the quiet material toward the threshold - detail without squash.",
          { { id::style, (float) Style::upward }, { id::threshold, -35.0f },
            { id::ratio, 2.0f }, { id::knee, 12.0f }, { id::range, 9.0f } } },

        { "Kick Duck (Host Sync)", "Effects", "sidechain;duck;pump;sync",
          "Tempo-locked quarter-note ducking from the Host Sync trigger - no kick routing needed.",
          { { id::triggerMode, (float) TriggerMode::hostSync },
            { id::syncRate, 3.0f }, { id::pulseLength, 25.0f },
            { id::threshold, -30.0f }, { id::ratio, 6.0f },
            { id::attack, 2.0f }, { id::release, 120.0f } } },

        { "External Duck", "Effects", "sidechain;external;duck",
          "Classic external-sidechain ducking: feed the trigger to the sidechain input.",
          { { id::scSource, 1.0f }, { id::threshold, -28.0f },
            { id::ratio, 5.0f }, { id::attack, 3.0f }, { id::release, 150.0f } } },
    };
    return c;
}

} // namespace mrdn
