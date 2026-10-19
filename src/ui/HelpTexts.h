#pragma once

#include <juce_core/juce_core.h>

namespace mrdn::help
{

// One string per control, used three ways (v0.8): hover tooltips, the
// help-hint bar at the bottom of the window, and as the accessibility help
// text screen readers announce. Keep them one to two sentences, plain
// language, and mention interactions with other controls where they bite.

inline constexpr const char* threshold =
    "Threshold: level where compression starts. Signal above it is reduced; "
    "with Auto Threshold on, this becomes an offset around the tracked programme level.";
inline constexpr const char* ratio =
    "Ratio: how hard the signal is compressed above the threshold. 2:1 is gentle "
    "levelling, 10:1 and beyond approaches limiting.";
inline constexpr const char* attack =
    "Attack: how fast compression clamps down once the signal crosses the threshold. "
    "Slow values let transients punch through; goes down to 0.02 ms.";
inline constexpr const char* release =
    "Release: how fast gain recovers when the signal falls below the threshold. "
    "A REL switches to programme-dependent auto release.";
inline constexpr const char* knee =
    "Knee: how gradually compression fades in around the threshold, 0 dB (hard) "
    "to 72 dB (very soft). Soft knees sound smoother on complex material.";
inline constexpr const char* makeup =
    "Makeup Gain: static output gain to bring the compressed level back up. "
    "AUTO GAIN (in Advanced) compensates automatically instead.";
inline constexpr const char* mix =
    "Mix: dry/wet blend for parallel compression up to 100%; beyond 100% it "
    "over-applies the computed gain reduction instead.";
inline constexpr const char* range =
    "Range: hard cap on the amount of gain change, in dB. Keeps extreme settings "
    "(and upward compression) under control.";
inline constexpr const char* styleBox =
    "Compression style: 14 characters from clean Digital to Vari-Mu and Opto. "
    "Each remaps detector, ballistics, and knee behaviour behind the same knobs.";
inline constexpr const char* detector =
    "Detector: Peak reacts to instantaneous level, RMS to average loudness "
    "(slower, smoother, more musical).";
inline constexpr const char* oversampling =
    "Oversampling: runs the compressor and character stage at up to 32x the "
    "sample rate for cleaner fast ballistics and saturation. Adds latency; watch the CPU readout.";
inline constexpr const char* autoRelease =
    "Auto Release: brief peaks recover quickly while sustained material releases "
    "slowly - a programme-dependent dual envelope.";
inline constexpr const char* autoThreshold =
    "Auto Threshold: the threshold follows the tracked programme loudness, so the "
    "same settings work at any input level. Dialog-oriented.";
inline constexpr const char* bypass =
    "Bypass: true click-free bypass (crossfaded, latency-aligned). Also on the B key.";
inline constexpr const char* undo = "Undo the last change (Ctrl+Z). Each A/B slot keeps its own history.";
inline constexpr const char* redo = "Redo (Ctrl+Shift+Z or Ctrl+Y).";

// Header: presets & A/B.
inline constexpr const char* presetName =
    "Preset browser: search, tags, favorites, factory and user presets (P). "
    "Shows the current preset's name.";
inline constexpr const char* presetPrev = "Previous preset ([).";
inline constexpr const char* presetNext = "Next preset (]).";
inline constexpr const char* slotA =
    "A/B comparison, state A (A key). Each slot is a full independent setting "
    "of every control with its own undo history.";
inline constexpr const char* slotB =
    "A/B comparison, state B (B key). Switch freely; nothing is lost on either side.";
inline constexpr const char* copyAb =
    "Copy the current state onto the other A/B slot (C). The copy is undoable "
    "in that slot's history.";
inline constexpr const char* helpButton =
    "Help: open the manual, keyboard shortcuts (F1), hover hints (H), "
    "high-contrast mode, and text size.";

// Advanced panel.
inline constexpr const char* hold =
    "Hold: freezes the detector between attack and release for up to 500 ms - "
    "steadies gain on percussive material.";
inline constexpr const char* lookahead =
    "Lookahead: the detector hears up to 20 ms early so attack can catch "
    "transients cleanly. Adds exactly that much latency (reported to the host).";
inline constexpr const char* drive =
    "Drive: input level into the character stage. Gain-compensated, so it adds "
    "colour rather than volume.";
inline constexpr const char* inTrim = "Input trim: +/-24 dB before everything else.";
inline constexpr const char* outTrim = "Output trim: +/-24 dB at the very end of the chain.";
inline constexpr const char* character =
    "Character: analog modelling - Clean (transparent), Warm (asymmetric), "
    "Tape (symmetric tanh), Drift (Warm plus slow gain wobble).";
inline constexpr const char* characterPos =
    "Character position: saturate before or after the compression stage.";
inline constexpr const char* autoGain =
    "Intelligent Auto Gain: tracks the gain reduction actually applied over ~2 s "
    "and compensates the output smoothly.";
inline constexpr const char* linkMode =
    "Stereo link mode: normal L/R, or M/S processing - compress only Mid or Side, "
    "or detect one and compress the other.";
inline constexpr const char* linkAmount =
    "Stereo link amount: 100% = both channels share one detector; 0% = fully "
    "independent per-channel compression.";

// Sidechain panel.
inline constexpr const char* scSource =
    "Detector source: Internal (the input itself) or External (the sidechain "
    "input bus from your DAW routing).";
inline constexpr const char* audition =
    "Audition: listen to the post-EQ detector signal the compressor actually "
    "reacts to, instead of the programme.";
inline constexpr const char* addBand =
    "Add a sidechain EQ band. The EQ shapes only what the detector hears - "
    "the audio path stays untouched.";
inline constexpr const char* scBandType =
    "Band type: Bell, shelves, cuts (incl. 16th-order brickwall), notch, "
    "band-pass, or all-pass. Detector path only.";
inline constexpr const char* scBandChan =
    "Which channel this band EQs in the detector: Both, Mid only, or Side only.";
inline constexpr const char* scBandFreq = "Band frequency (20 Hz - 20 kHz).";
inline constexpr const char* scBandGain = "Band gain (+/-24 dB, where the type uses gain).";
inline constexpr const char* scBandQ = "Band width/resonance (Q 0.1 - 18).";
inline constexpr const char* scBandRemove = "Remove this band.";
inline constexpr const char* triggerMode =
    "Trigger: what fires the detector - the audio itself, a tempo-locked pulse "
    "train (Host Sync), or MIDI notes.";
inline constexpr const char* syncRate =
    "Sync rate: the tempo division of the trigger pulse train (with dotted and "
    "triplet values).";
inline constexpr const char* syncOffset =
    "Sync offset: shifts the pulse within its division, 0-100% of a cycle.";
inline constexpr const char* pulseLength =
    "Pulse length: how much of each division the trigger holds the detector "
    "open - sets the duck's duty cycle.";

// Panels & misc.
inline constexpr const char* advancedToggle =
    "Advanced: gain-shaping utilities (hold, lookahead, drive, trims), the "
    "character stage, and stereo link modes.";
inline constexpr const char* sidechainToggle =
    "Sidechain: detector source, audition, tempo/MIDI triggering, and the "
    "detector-only EQ. Lights up when any of it is active.";
inline constexpr const char* mainDisplayText =
    "Level history with applied gain reduction traced from the top in amber; the "
    "top-right panel is the live transfer curve. Threshold/knee overlays follow the knobs.";
inline constexpr const char* meterStrip =
    "IN / GR / OUT metering with peak-hold and clip indicators (click a clip "
    "light to reset it). Below: EBU R128 momentary loudness.";

// Keyboard shortcuts, shown in the F1 overlay and the manual. Kept as
// data so the overlay and the docs can't drift apart.
struct Shortcut { const char* keys; const char* action; };
inline constexpr Shortcut shortcuts[] = {
    { "Ctrl+Z",              "Undo (active A/B slot)" },
    { "Ctrl+Shift+Z / Ctrl+Y", "Redo" },
    { "A / B",               "Switch to state A / B" },
    { "C",                   "Copy current state to the other slot" },
    { "P",                   "Open / close the preset browser" },
    { "[ / ]",               "Previous / next preset" },
    { "Shift+B",             "Toggle bypass" },
    { "V",                   "Toggle Advanced panel" },
    { "S",                   "Toggle Sidechain panel" },
    { "H",                   "Toggle help hints bar" },
    { "F1",                  "Keyboard shortcuts overlay" },
    { "Esc",                 "Close overlay / browser" },
    { "Tab / Shift+Tab",     "Move keyboard focus between controls" },
    { "Arrow keys",          "Adjust the focused knob or slider" },
};

// The manual ships inside the plugin binary (there is no installer yet at
// v0.8); "Open Manual" writes it next to the settings file and opens the
// default browser.
inline const char* manualHtml()
{
    return R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<title>Meridian Manual</title>
<style>
 body { font-family: Segoe UI, Helvetica, Arial, sans-serif; background:#15181e; color:#d8dce4;
        max-width: 60em; margin: 2em auto; padding: 0 1.5em; line-height: 1.55; }
 h1, h2, h3 { color: #46b5dc; font-weight: 600; }
 h1 { border-bottom: 1px solid #313845; padding-bottom: .3em; }
 code, kbd { background:#242a34; border-radius:3px; padding:.1em .4em; color:#d8dce4; }
 table { border-collapse: collapse; margin: 1em 0; }
 th, td { border: 1px solid #313845; padding: .35em .7em; text-align: left; }
 th { background:#1d2129; }
 .gr { color:#e09a44; } .accent { color:#46b5dc; }
</style></head><body>

<h1>Meridian</h1>
<p>Meridian is a professional compressor with 14 compression styles, deep sidechain
control, tempo and MIDI triggering, analog character stages, and flagship metering.
This manual covers every control; hover any control in the plugin (or press
<kbd>H</kbd> for the hints bar) for the short version in place.</p>

<h2>Quick start</h2>
<ol>
<li>Pick a <b>Style</b> for the job (Vocal, Bus, Mastering, ...) or start from a preset
    (<kbd>P</kbd> opens the browser).</li>
<li>Lower <b>Threshold</b> until the amber <span class="gr">GR</span> trace shows a few dB
    of reduction on the loud parts.</li>
<li>Shape it with <b>Ratio</b>, <b>Attack</b>, <b>Release</b>, and <b>Knee</b>.</li>
<li>Restore level with <b>Makeup</b> (or Advanced &gt; <b>AUTO GAIN</b>).</li>
<li>Compare candidate settings with <b>A/B</b>; each slot keeps its own undo history.</li>
</ol>

<h2>Compression styles</h2>
<table>
<tr><th>Style</th><th>Character</th></tr>
<tr><td>Versatile</td><td>Clean all-rounder; the default.</td></tr>
<tr><td>Smooth</td><td>Gentle RMS levelling with programme-dependent release.</td></tr>
<tr><td>Vari-Mu</td><td>Tube-style: slow, wide knee, ratio grows with drive.</td></tr>
<tr><td>Opto</td><td>Optical: two-stage release that depends on how hard it worked.</td></tr>
<tr><td>Punch</td><td>Transient-forward; attack eases open on peaks.</td></tr>
<tr><td>Upward</td><td>Raises quiet material below the threshold (cap with Range).</td></tr>
<tr><td>Maximize</td><td>To-the-max loudness slam.</td></tr>
<tr><td>Bus</td><td>Mix-bus glue; slow and forgiving.</td></tr>
<tr><td>Mastering</td><td>Transparent, slow, low-ratio precision.</td></tr>
<tr><td>Pump</td><td>Aggressive fast-release pumping.</td></tr>
<tr><td>De-Ess</td><td>HF-weighted detection for sibilance.</td></tr>
<tr><td>Limit</td><td>Hard limiting, effectively infinite ratio.</td></tr>
<tr><td>Vocal</td><td>Dialog/vocal-tuned levelling.</td></tr>
<tr><td>Digital</td><td>Classic hard-knee digital: exactly your numbers.</td></tr>
</table>

<h2>Main controls</h2>
<p><b>Threshold</b> sets where compression begins. With <b>A THR</b> (Auto Threshold)
engaged it becomes an offset relative to the tracked programme loudness, so the amount
of compression stays constant when the source gets hotter or quieter.
<b>Ratio</b> sets the slope above the threshold, and <b>Knee</b> (0&ndash;72&nbsp;dB) how
gradually that slope arrives. <b>Attack</b> (down to 0.02&nbsp;ms) and <b>Release</b>
set the ballistics; <b>A REL</b> switches to a dual-envelope auto release where brief
peaks recover fast but sustained reduction releases slowly.</p>
<p><b>Mix</b> blends dry and compressed up to 100% (parallel compression); past 100%
it scales the applied gain reduction instead (over-compression). <b>Range</b> caps the
maximum gain change. <b>Makeup</b> is static output gain; <b>AUTO GAIN</b> (Advanced)
tracks the applied reduction and compensates smoothly. <b>Detector</b> chooses Peak or
RMS level sensing, and <b>Oversample</b> runs the engine at up to 32x for cleaner fast
ballistics and saturation (latency is reported to the host; watch the CPU readout).</p>

<h2>Advanced panel</h2>
<p><b>Hold</b> freezes the envelope between attack and release (up to 500&nbsp;ms).
<b>Lookahead</b> (up to 20&nbsp;ms) lets the detector act before the transient arrives.
<b>Drive</b> pushes the <b>Character</b> stage &mdash; Clean, Warm (asymmetric), Tape
(symmetric), or Drift (Warm plus slow analog wobble) &mdash; placed <b>Pre</b> or
<b>Post</b> compression, inside the oversampled path. <b>IN/OUT</b> trims add up to
&plusmn;24&nbsp;dB either side of the chain.</p>
<p><b>Link</b> selects stereo behaviour: <i>Stereo</i> with a variable link amount
(100% = one shared detector), or M/S modes that compress only Mid or Side, or detect
one channel and compress the other. The uncompressed channel stays time-aligned.</p>

<h2>Sidechain &amp; triggering</h2>
<p><b>Source</b> switches the detector between the input itself and the external
sidechain bus. The <b>sidechain EQ</b> (up to 6 bands, including 16th-order brickwall
cuts, notch, band-pass, all-pass; each band can target Both/Mid/Side) shapes only what
the detector hears. <b>AUDITION</b> monitors that detector signal.
<b>Trigger</b> replaces audio detection with a tempo-locked pulse train
(<i>Host Sync</i>: rate, offset, pulse length) or <i>MIDI</i> note gating &mdash; with a
0&nbsp;dBFS pulse, Threshold sets the ducking depth and Attack/Release shape it.</p>

<h2>Presets, A/B, and undo</h2>
<p>The preset browser (<kbd>P</kbd>, or click the preset name) searches names, tags, and
categories; star any preset to collect it under <b>Favorites</b>. Factory presets are
built in; user presets are plain XML <code>.mrdn</code> files in
<code>Documents/Meridian/Presets</code> &mdash; friendly to backups and file sync, and
portable with <b>IMPORT</b>/<b>EXPORT</b>. <b>SAVE AS</b> stores the current state with
name, author, category, tags, and description.</p>
<p><b>A/B</b> keeps two complete states. Switching is instant and lossless, <b>COPY</b>
pushes the current state onto the other slot, and each slot keeps an
<i>independent</i> undo history (<kbd>Ctrl+Z</kbd>/<kbd>Ctrl+Shift+Z</kbd>).</p>

<h2>MIDI Learn</h2>
<p>Right-click any knob &rarr; <i>MIDI Learn</i>, then move a controller. One CC per
parameter, one parameter per CC; mappings persist with the session and inside presets
saved from a mapped state.</p>

<h2>Keyboard shortcuts</h2>
<table><tr><th>Keys</th><th>Action</th></tr>
<tr><td><kbd>Ctrl+Z</kbd></td><td>Undo (active A/B slot)</td></tr>
<tr><td><kbd>Ctrl+Shift+Z</kbd> / <kbd>Ctrl+Y</kbd></td><td>Redo</td></tr>
<tr><td><kbd>A</kbd> / <kbd>B</kbd></td><td>Switch to state A / B</td></tr>
<tr><td><kbd>C</kbd></td><td>Copy current state to the other slot</td></tr>
<tr><td><kbd>P</kbd></td><td>Open / close the preset browser</td></tr>
<tr><td><kbd>[</kbd> / <kbd>]</kbd></td><td>Previous / next preset</td></tr>
<tr><td><kbd>Shift+B</kbd></td><td>Toggle bypass</td></tr>
<tr><td><kbd>V</kbd></td><td>Toggle Advanced panel</td></tr>
<tr><td><kbd>S</kbd></td><td>Toggle Sidechain panel</td></tr>
<tr><td><kbd>H</kbd></td><td>Toggle help hints bar</td></tr>
<tr><td><kbd>F1</kbd></td><td>Keyboard shortcuts overlay</td></tr>
<tr><td><kbd>Esc</kbd></td><td>Close overlay / browser</td></tr>
<tr><td><kbd>Tab</kbd> / <kbd>Shift+Tab</kbd></td><td>Move focus between controls</td></tr>
<tr><td>Arrow keys</td><td>Adjust the focused control</td></tr>
</table>

<h2>Accessibility</h2>
<p>Every control is keyboard-reachable (<kbd>Tab</kbd> to focus &mdash; the focused
control shows a cyan ring &mdash; arrow keys to adjust) and carries a name and
description for screen readers. The HELP menu offers a <b>high-contrast</b> palette
and <b>text size</b> up to 130%. The window is freely resizable by dragging its edge
or corner (0.5x&ndash;2x), independent of system DPI scaling.</p>

<h2>Metering</h2>
<p>The main display scrolls input/output level history with applied gain reduction
traced from the top in amber, alongside a live transfer curve with the operating point
riding it. The meter strip shows IN/GR/OUT with peak-hold and clip indicators (click to
reset) plus EBU&nbsp;R128 momentary loudness. External sidechain signals trace in violet
when connected.</p>

</body></html>
)HTML";
}

} // namespace mrdn::help
