#pragma once

#include "PluginProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/Meters.h"
#include "ui/MainDisplay.h"
#include "ui/PresetBrowser.h"
#include <juce_opengl/juce_opengl.h>
#include <map>

// All of the UI lives in this content component at a fixed logical size;
// the editor shell scales it with an AffineTransform driven by dragging the
// window edge/corner. Keeping the user scale on an inner component leaves
// AudioProcessorEditor::setScaleFactor free for the host's DPI handling, so
// the two multiply instead of fighting.
class MeridianEditorContent : public juce::Component,
                              private juce::Timer
{
public:
    MeridianEditorContent (MeridianAudioProcessor&, juce::PropertiesFile&,
                           mrdn::LookAndFeel&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // MIDI Learn menu
    void mouseMove (const juce::MouseEvent&) override;   // help hints bar
    bool keyPressed (const juce::KeyPress&) override;    // shortcuts

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name,
                    const juce::String& tooltip);
    void timerCallback() override;

    // Section state ---------------------------------------------------------
    bool isBandOn (int band) const;
    void setBandOn (int band, bool on);
    bool sidechainInUse() const;
    void refreshLayout();   // visibility + content height for the open panels

    // v0.8 helpers ------------------------------------------------------------
    void toggleBoolParam (const juce::String& paramId);
    void showHelpMenu();
    void openManual();
    void setHelpHints (bool on);
    void setHighContrast (bool on);
    void setTextScale (float scale);
    void applyStaticColours();   // explicit colours, re-applied on palette swap

    MeridianAudioProcessor& processor;
    juce::PropertiesFile& settings;
    mrdn::LookAndFeel& lookAndFeel;

    // Header.
    juce::Rectangle<int> logoArea;   // product mark, painted in paint()
    juce::Label title;
    juce::ComboBox styleBox;
    juce::TextButton bypassButton { "BYPASS" };
    juce::ShapeButton undoButton { "undo", juce::Colours::grey, juce::Colours::white,
                                   juce::Colours::white };
    juce::ShapeButton redoButton { "redo", juce::Colours::grey, juce::Colours::white,
                                   juce::Colours::white };
    juce::TextButton slotAButton { "A" }, slotBButton { "B" }, copyAbButton;
    juce::TextButton prevPresetButton, presetNameButton, nextPresetButton;
    juce::TextButton helpButton { "HELP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Primary controls (always visible).
    Knob threshold, ratio, attack, release, knee, makeup, mix, range;
    juce::ComboBox detectorBox, oversamplingBox;
    juce::Label detectorLabel, oversamplingLabel, cpuLabel;
    juce::TextButton autoReleaseButton { "A REL" }, autoThresholdButton { "A THR" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> detectorAttachment,
                                                                            oversamplingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoReleaseAttachment,
                                                                          autoThresholdAttachment;

    // Section toggles (pure UI state, not parameters).
    juce::TextButton advancedToggle, sidechainToggle;
    bool advancedOpen = false, sidechainOpen = false, scInUseIndicator = false;
    juce::Rectangle<int> advancedPanelArea, sidechainPanelArea;

    // Advanced panel: gain-shaping utilities, character, stereo link.
    Knob hold, lookahead, drive, inTrim, outTrim;
    juce::ComboBox characterBox, characterPosBox, linkModeBox;
    juce::Label characterLabel, linkLabel;
    juce::Slider linkAmountSlider;
    juce::TextButton autoGainButton { "AUTO GAIN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> characterAttachment,
                                                                            characterPosAttachment,
                                                                            linkModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> linkAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment;

    // Sidechain panel. Only bands whose "on" parameter is set get a cell;
    // the remove button clears that parameter and the cell disappears.
    struct ScBandUI
    {
        juce::TextButton remove;
        juce::ComboBox type, chan;
        juce::Slider freq, gain, q;
        juce::Label freqLabel, gainLabel, qLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt, chanAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAtt, gainAtt, qAtt;
    };
    std::array<ScBandUI, mrdn::numScBands> scBands;
    uint32_t visibleBandMask = 0;   // polled; relayout when it changes

    juce::Label scSourceLabel, scEmptyLabel;
    juce::ComboBox scSourceBox;
    juce::TextButton auditionButton { "AUDITION" };
    juce::TextButton addBandButton { "+ BAND" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scSourceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> auditionAttachment;

    // Trigger row (in the sidechain panel): mode always shown; the sync
    // rate/offset/length controls appear only in Host Sync mode.
    juce::Label triggerLabel, syncOffsetLabel, pulseLengthLabel;
    juce::ComboBox triggerModeBox, syncRateBox;
    juce::Slider syncOffsetSlider, pulseLengthSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> triggerModeAttachment,
                                                                            syncRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> syncOffsetAttachment,
                                                                          pulseLengthAttachment;
    int shownTriggerMode = 0;   // polled; relayout when it changes

    // MIDI Learn: right-clickable controls -> parameter IDs.
    std::map<const juce::Component*, juce::String> midiLearnTargets;

    // Main display (v0.6): scrolling level history + animated knee panel.
    mrdn::MainDisplay mainDisplay { processor };

    // Metering.
    mrdn::LevelMeter inMeter, outMeter;
    mrdn::GainReductionMeter grMeter;
    juce::Label inMeterLabel, grMeterLabel, outMeterLabel, grReadout, lufsReadout;

    // Help hints bar (v0.8): shows the hovered control's help text.
    juce::Label hintBar;
    bool helpHintsOn = false;

    // One-shot: arm keyboard shortcuts when the editor first shows.
    bool initialFocusGrabbed = false;

    // Keyboard shortcuts overlay (F1).
    struct ShortcutsOverlay : juce::Component
    {
        ShortcutsOverlay() { setWantsKeyboardFocus (true); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
        bool keyPressed (const juce::KeyPress&) override { setVisible (false); return true; }
    };
    ShortcutsOverlay shortcutsOverlay;

    // Preset browser overlay (v0.8).
    mrdn::PresetBrowser presetBrowser { processor.presets };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeridianEditorContent)
};

// Thin shell: hosts the content component and is freely resizable — drag
// the window edge or corner and the whole UI scales proportionally (the
// aspect ratio is pinned to the content's logical shape). The continuous
// scale persists per-user in the Equinox Audio settings file, not in
// plugin state, so presets don't resize the window. All painting is
// driven through an OpenGL context.
class MeridianAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit MeridianAudioProcessorEditor (MeridianAudioProcessor&);
    ~MeridianAudioProcessorEditor() override;

    void resized() override;
    void childBoundsChanged (juce::Component*) override;

    // Hosts (and the standalone window) hand keyboard focus to the editor
    // itself; keys bubble up from there, so without this the shortcuts
    // would only work after a click moved focus into the content.
    bool keyPressed (const juce::KeyPress& key) override
    {
        return content.keyPressed (key);
    }

private:
    void updateSizeConstraints();

    mrdn::LookAndFeel lookAndFeel;
    juce::PropertiesFile settings;
    MeridianEditorContent content;
    juce::OpenGLContext openGLContext;
    juce::TooltipWindow tooltipWindow { this, 700 };
    float uiScale = 1.0f;
    bool followingContentResize = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeridianAudioProcessorEditor)
};
