#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "StateManager.h"
#include "presets/PresetManager.h"
#include "dsp/CompressorEngine.h"
#include "dsp/CharacterStage.h"
#include "dsp/SidechainEq.h"
#include "dsp/LoudnessMeter.h"

class MeridianAudioProcessor : public juce::AudioProcessor
{
public:
    MeridianAudioProcessor();
    ~MeridianAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Meridian"; }
    bool acceptsMidi() const override { return true; }  // trigger + MIDI Learn
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Saved-state schema version, embedded in every state/preset we write.
    // States from a newer schema are ignored rather than half-applied — the
    // standalone app in particular keeps stale state across dev builds.
    // v2 (v0.8): session root carries both A/B slots; presets gained a
    // metadata wrapper. v1 states/presets still load.
    static constexpr int stateVersion = 2;
    bool applyStateXml (const juce::XmlElement&);

    // MIDI Learn --------------------------------------------------------------
    // Arm on the message thread; the audio thread captures the next CC it
    // sees and starts applying it immediately. The mapping is persisted into
    // the state tree by commitPendingMidiLearn(), which must run on the
    // message thread — the editor timer calls it, and getStateInformation
    // calls it too so a headless session save still keeps the mapping.
    void armMidiLearn (const juce::String& paramId);
    void cancelMidiLearn() { learnTarget.store (nullptr); }
    bool isMidiLearnArmed() const { return learnTarget.load() != nullptr; }
    int getMappedCc (const juce::String& paramId) const;   // -1 if unmapped
    void clearMidiMapping (const juce::String& paramId);
    void commitPendingMidiLearn();

    juce::AudioProcessorValueTreeState apvts;

    // v0.8: snapshot-based undo/redo with independent history per A/B slot
    // (replaces the juce::UndoManager wiring), plus the preset ecosystem.
    mrdn::StateManager stateManager { *this, apvts, juce::Identifier ("MeridianSession") };
    mrdn::PresetManager presets { apvts, mrdn::makePresetConfig(),
        [this] (const juce::ValueTree& t) { stateManager.applyState (t); },
        [this] { return stateManager.getCurrentPresetName(); },
        [this] (const juce::String& n) { stateManager.setCurrentPresetName (n); } };

    // Metering values for the GUI. Gain reduction is the block maximum in dB
    // (positive = reducing); peaks are held since the last exchange (0).
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> peakIn { 0.0f }, peakOut { 0.0f };

    // Smoothed processing load as a fraction of realtime (0..1+), for the
    // CPU readout next to the oversampling control.
    std::atomic<float> cpuLoad { 0.0f };

    // Whether an external sidechain is connected (for the GUI).
    std::atomic<bool> sidechainActive { false };

    // EBU R128 / BS.1770 momentary loudness of the output, in LUFS.
    mrdn::LoudnessMeter loudness;

    // Lock-free level history for the main display: one frame per
    // scopeCellSamples samples (max |in|, max |out|, applied GR in dB,
    // max |post-EQ detector| for the sidechain trace).
    // The GUI reads frames between its last index and scopeWriteIndex.
    struct ScopeFrame { float inPk, outPk, grDb, scPk; };
    static constexpr int scopeSize = 4096; // power of two
    static constexpr int scopeCellSamples = 256;
    std::array<ScopeFrame, scopeSize> scopeRing;
    std::atomic<uint32_t> scopeWriteIndex { 0 };

private:
    mrdn::CompressorEngine engine;
    mrdn::CharacterStage character;
    mrdn::SidechainEq scEq;

    juce::SmoothedValue<float> inTrimSmooth, outTrimSmooth, bypassMix, autoGainSmooth;

    // Smart parameter interpolation: audible static parameters ramp over
    // ~20 ms instead of stepping per block.
    juce::SmoothedValue<float> thresholdSmooth, ratioSmooth, kneeSmooth, makeupSmooth,
                               mixSmooth, rangeSmooth, driveSmooth;

    // Slow tracker of applied GR that drives Intelligent Auto Gain.
    float autoGainTrackDb = 0.0f;

    // Scope cell accumulation (audio thread only).
    float scopeAccIn = 0.0f, scopeAccOut = 0.0f, scopeAccGr = 0.0f, scopeAccSc = 0.0f;
    int scopeCellPos = 0;

    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* ratioParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* kneeParam = nullptr;
    std::atomic<float>* makeupParam = nullptr;
    std::atomic<float>* inTrimParam = nullptr;
    std::atomic<float>* outTrimParam = nullptr;
    std::atomic<float>* detectorParam = nullptr;
    std::atomic<float>* styleParam = nullptr;
    std::atomic<float>* linkAmountParam = nullptr;
    std::atomic<float>* linkModeParam = nullptr;
    std::atomic<float>* scSourceParam = nullptr;
    std::atomic<float>* auditionParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* autoReleaseParam = nullptr;
    std::atomic<float>* autoThresholdParam = nullptr;
    std::atomic<float>* oversamplingParam = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* characterPosParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* rangeParam = nullptr;
    std::atomic<float>* holdParam = nullptr;
    std::atomic<float>* lookaheadParam = nullptr;
    std::atomic<float>* autoGainParam = nullptr;
    std::atomic<float>* triggerModeParam = nullptr;
    std::atomic<float>* syncRateParam = nullptr;
    std::atomic<float>* syncOffsetParam = nullptr;
    std::atomic<float>* pulseLengthParam = nullptr;

    // MIDI trigger gate: counts held notes across block boundaries.
    int heldNotes = 0;
    float midiGate = 0.0f;

    // MIDI Learn plumbing. ccTargets is the audio-thread lookup (128 CCs);
    // the state tree child "MIDIMAP" is the persistent source of truth.
    std::array<std::atomic<juce::RangedAudioParameter*>, 128> ccTargets {};
    std::atomic<juce::RangedAudioParameter*> learnTarget { nullptr };
    std::atomic<juce::RangedAudioParameter*> pendingLearnParam { nullptr };
    std::atomic<int> pendingLearnCc { -1 };
    void rebuildMidiCcMap();   // message thread
    void handleMidi (juce::MidiBuffer&);

    struct ScBandParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* chan = nullptr;
    };
    std::array<ScBandParams, mrdn::numScBands> scBandParams;

    juce::AudioBuffer<float> dryBuffer, detBuffer;

    // One oversampler per selectable factor (index 1..5 -> 2x..32x); index 0
    // (Off) processes at the host rate. All are prepared up front so the
    // factor can change at block boundaries without allocation. The detector
    // bank is upsample-only (the detector never returns to the host rate).
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 6> oversamplers;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 6> detOversamplers;
    int currentOsChoice = 0;
    double hostSampleRate = 48000.0;
    int lastReportedLatency = -1;

    void applyOversamplingChoice (int choice);
    void updateLatency();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeridianAudioProcessor)
};
