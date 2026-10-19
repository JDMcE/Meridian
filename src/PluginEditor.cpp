#include "PluginEditor.h"
#include "Parameters.h"
#include "ui/HelpTexts.h"

namespace
{
    // Fixed section heights in logical units; the content height is their
    // sum for whichever panels are open. The editor shell scales the lot.
    constexpr int editorWidth   = 920;
    constexpr int meterStripW   = 150;
    constexpr int headerH       = 46;
    constexpr int displayH      = 236;
    constexpr int bigRowH       = 138;
    constexpr int row2H         = 112;
    constexpr int toggleBarH    = 30;
    constexpr int advancedH     = 118;
    constexpr int sidechainH    = 158;   // header + trigger row + band cells
    constexpr int hintBarH      = 22;    // help hints strip (when enabled)
    constexpr int bottomPad     = 8;

    // Free-resize scale range (1.0 is the design's native size). The window
    // aspect ratio is pinned to the content, so the scale is the single
    // degree of freedom an edge/corner drag moves.
    constexpr float minUiScale = 0.5f;
    constexpr float maxUiScale = 2.0f;

    // The v0.6 builds stored a discrete size index; map it onto the
    // continuous scale once, then forget it.
    constexpr float legacyUiScales[] = { 0.75f, 0.9f, 1.0f, 1.2f };

    juce::PropertiesFile::Options makeSettingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Meridian";
        o.folderName = "Equinox Audio";
        o.filenameSuffix = "settings";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    // Circular-arrow icon for undo/redo: a clockwise arc with an arrowhead
    // at its end; mirrored for undo.
    juce::Path makeHistoryArrow (bool redo)
    {
        juce::Path arc;
        arc.addCentredArc (0.0f, 0.0f, 8.0f, 8.0f, 0.0f,
                           juce::MathConstants<float>::pi * 0.30f,
                           juce::MathConstants<float>::pi * 1.70f, true);
        juce::Path icon;
        juce::PathStrokeType (2.6f).createStrokedPath (icon, arc);

        const float endAngle = juce::MathConstants<float>::pi * 1.70f;
        const juce::Point<float> tip (8.0f * std::sin (endAngle),
                                      -8.0f * std::cos (endAngle));
        const juce::Point<float> dir (std::cos (endAngle), std::sin (endAngle));
        const juce::Point<float> perp (-dir.y, dir.x);

        juce::Path head;
        head.addTriangle (tip + dir * 5.5f,
                          tip - dir * 1.0f + perp * 4.0f,
                          tip - dir * 1.0f - perp * 4.0f);
        icon.addPath (head);

        if (! redo)
            icon.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f));
        return icon;
    }
}

//==============================================================================
// Content

MeridianEditorContent::MeridianEditorContent (MeridianAudioProcessor& p,
                                              juce::PropertiesFile& settingsToUse,
                                              mrdn::LookAndFeel& lnf)
    : processor (p), settings (settingsToUse), lookAndFeel (lnf)
{
    // Accessibility prefs must be live before any child is created, so
    // controls pick up the right palette/fonts from the start.
    mrdn::colors::setHighContrast (settings.getBoolValue ("high_contrast", false));
    lookAndFeel.applyPalette();
    lookAndFeel.setTextScale ((float) settings.getDoubleValue ("text_scale", 1.0));
    helpHintsOn = settings.getBoolValue ("help_hints", false);

    setWantsKeyboardFocus (true); // header shortcuts (see keyPressed)
    setTitle ("Meridian Compressor");
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    // Header ---------------------------------------------------------------
    title.setText ("MERIDIAN", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold)));
    addAndMakeVisible (title);

    styleBox.addItemList (mrdn::styleChoices, 1);
    styleBox.setTitle ("Compression style");
    styleBox.setTooltip (mrdn::help::styleBox);
    addAndMakeVisible (styleBox);
    styleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, mrdn::id::style, styleBox);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTitle ("Bypass");
    bypassButton.setTooltip (mrdn::help::bypass);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, mrdn::id::bypass, bypassButton);

    undoButton.setShape (makeHistoryArrow (false), false, true, false);
    redoButton.setShape (makeHistoryArrow (true), false, true, false);
    undoButton.setBorderSize (juce::BorderSize<int> (6));
    redoButton.setBorderSize (juce::BorderSize<int> (6));
    undoButton.setTitle ("Undo");
    redoButton.setTitle ("Redo");
    undoButton.setTooltip (mrdn::help::undo);
    redoButton.setTooltip (mrdn::help::redo);
    undoButton.onClick = [this] { processor.stateManager.undo(); };
    redoButton.onClick = [this] { processor.stateManager.redo(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    // A/B comparison.
    slotAButton.onClick = [this] { processor.stateManager.setActiveSlot (0); };
    slotBButton.onClick = [this] { processor.stateManager.setActiveSlot (1); };
    copyAbButton.onClick = [this] { processor.stateManager.copyActiveToOther(); };
    slotAButton.setTitle ("Switch to state A");
    slotBButton.setTitle ("Switch to state B");
    copyAbButton.setTitle ("Copy state to the other slot");
    slotAButton.setTooltip (mrdn::help::slotA);
    slotBButton.setTooltip (mrdn::help::slotB);
    copyAbButton.setTooltip (mrdn::help::copyAb);
    copyAbButton.setButtonText (juce::String::fromUTF8 ("A\xe2\x86\x92""B"));
    addAndMakeVisible (slotAButton);
    addAndMakeVisible (slotBButton);
    addAndMakeVisible (copyAbButton);

    // Preset navigation.
    prevPresetButton.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xb9"));
    nextPresetButton.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xba"));
    prevPresetButton.setTitle ("Previous preset");
    nextPresetButton.setTitle ("Next preset");
    presetNameButton.setTitle ("Preset browser");
    prevPresetButton.setTooltip (mrdn::help::presetPrev);
    nextPresetButton.setTooltip (mrdn::help::presetNext);
    presetNameButton.setTooltip (mrdn::help::presetName);
    prevPresetButton.onClick = [this] { processor.presets.loadAdjacent (false); };
    nextPresetButton.onClick = [this] { processor.presets.loadAdjacent (true); };
    presetNameButton.onClick = [this]
    {
        if (presetBrowser.isVisible())
            presetBrowser.closeBrowser();
        else
            presetBrowser.openBrowser();
    };
    addAndMakeVisible (prevPresetButton);
    addAndMakeVisible (presetNameButton);
    addAndMakeVisible (nextPresetButton);

    helpButton.setTitle ("Help");
    helpButton.setTooltip (mrdn::help::helpButton);
    helpButton.onClick = [this] { showHelpMenu(); };
    addAndMakeVisible (helpButton);

    cpuLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (cpuLabel);

    // Primary controls -------------------------------------------------------
    setupKnob (threshold, mrdn::id::threshold, "THRESHOLD", mrdn::help::threshold);
    setupKnob (ratio,     mrdn::id::ratio,     "RATIO",     mrdn::help::ratio);
    setupKnob (attack,    mrdn::id::attack,    "ATTACK",    mrdn::help::attack);
    setupKnob (release,   mrdn::id::release,   "RELEASE",   mrdn::help::release);
    setupKnob (knee,      mrdn::id::knee,      "KNEE",      mrdn::help::knee);
    setupKnob (makeup,    mrdn::id::makeup,    "MAKEUP",    mrdn::help::makeup);
    setupKnob (mix,       mrdn::id::mix,       "MIX",       mrdn::help::mix);
    setupKnob (range,     mrdn::id::range,     "RANGE",     mrdn::help::range);

    detectorLabel.setText ("DETECT", juce::dontSendNotification);
    oversamplingLabel.setText ("OVERSAMPLE", juce::dontSendNotification);
    for (auto* l : { &detectorLabel, &oversamplingLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (11.0f)));
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*l);
    }

    detectorBox.addItemList (mrdn::detectorChoices, 1);
    detectorBox.setTitle ("Detector mode");
    detectorBox.setTooltip (mrdn::help::detector);
    addAndMakeVisible (detectorBox);
    detectorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, mrdn::id::detector, detectorBox);

    oversamplingBox.addItemList (mrdn::oversamplingChoices, 1);
    oversamplingBox.setTitle ("Oversampling");
    oversamplingBox.setTooltip (mrdn::help::oversampling);
    addAndMakeVisible (oversamplingBox);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, mrdn::id::oversampling, oversamplingBox);

    autoReleaseButton.setClickingTogglesState (true);
    autoReleaseButton.setTitle ("Auto release");
    autoReleaseButton.setTooltip (mrdn::help::autoRelease);
    addAndMakeVisible (autoReleaseButton);
    autoReleaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, mrdn::id::autoRelease, autoReleaseButton);

    autoThresholdButton.setClickingTogglesState (true);
    autoThresholdButton.setTitle ("Auto threshold");
    autoThresholdButton.setTooltip (mrdn::help::autoThreshold);
    addAndMakeVisible (autoThresholdButton);
    autoThresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, mrdn::id::autoThreshold, autoThresholdButton);

    // Section toggles ---------------------------------------------------------
    advancedToggle.setClickingTogglesState (true);
    advancedToggle.setTitle ("Advanced panel");
    advancedToggle.setTooltip (mrdn::help::advancedToggle);
    advancedToggle.onClick = [this]
    {
        advancedOpen = advancedToggle.getToggleState();
        refreshLayout();
    };
    addAndMakeVisible (advancedToggle);

    sidechainToggle.setClickingTogglesState (true);
    sidechainToggle.setTitle ("Sidechain panel");
    sidechainToggle.setTooltip (mrdn::help::sidechainToggle);
    sidechainToggle.onClick = [this]
    {
        sidechainOpen = sidechainToggle.getToggleState();
        refreshLayout();
    };
    addAndMakeVisible (sidechainToggle);

    // Advanced panel ----------------------------------------------------------
    setupKnob (hold,      mrdn::id::hold,      "HOLD",      mrdn::help::hold);
    setupKnob (lookahead, mrdn::id::lookahead, "LOOKAHEAD", mrdn::help::lookahead);
    setupKnob (drive,     mrdn::id::drive,     "DRIVE",     mrdn::help::drive);
    setupKnob (inTrim,    mrdn::id::inTrim,    "IN",        mrdn::help::inTrim);
    setupKnob (outTrim,   mrdn::id::outTrim,   "OUT",       mrdn::help::outTrim);

    characterLabel.setText ("CHARACTER", juce::dontSendNotification);
    linkLabel.setText ("LINK", juce::dontSendNotification);
    for (auto* l : { &characterLabel, &linkLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (11.0f)));
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*l);
    }

    characterBox.addItemList (mrdn::characterChoices, 1);
    characterBox.setTitle ("Character mode");
    characterBox.setTooltip (mrdn::help::character);
    addAndMakeVisible (characterBox);
    characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, mrdn::id::character, characterBox);

    characterPosBox.addItemList (mrdn::characterPosChoices, 1);
    characterPosBox.setTitle ("Character position");
    characterPosBox.setTooltip (mrdn::help::characterPos);
    addAndMakeVisible (characterPosBox);
    characterPosAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, mrdn::id::characterPos, characterPosBox);

    autoGainButton.setClickingTogglesState (true);
    autoGainButton.setTitle ("Auto gain");
    autoGainButton.setTooltip (mrdn::help::autoGain);
    addAndMakeVisible (autoGainButton);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, mrdn::id::autoGain, autoGainButton);

    linkModeBox.addItemList (mrdn::linkModeChoices, 1);
    linkModeBox.setTitle ("Stereo link mode");
    linkModeBox.setTooltip (mrdn::help::linkMode);
    addAndMakeVisible (linkModeBox);
    linkModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, mrdn::id::linkMode, linkModeBox);

    linkAmountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    linkAmountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 16);
    linkAmountSlider.setTitle ("Stereo link amount");
    linkAmountSlider.setTooltip (mrdn::help::linkAmount);
    addAndMakeVisible (linkAmountSlider);
    linkAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, mrdn::id::linkAmount, linkAmountSlider);

    // Sidechain panel -----------------------------------------------------------
    scSourceLabel.setText ("SOURCE", juce::dontSendNotification);
    scSourceLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (scSourceLabel);

    scEmptyLabel.setText (juce::String::fromUTF8 (
                              "NO EQ BANDS \xe2\x80\x94 the detector hears the signal flat"),
                          juce::dontSendNotification);
    scEmptyLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    scEmptyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (scEmptyLabel);

    scSourceBox.addItemList (mrdn::scSourceChoices, 1);
    scSourceBox.setTitle ("Sidechain source");
    scSourceBox.setTooltip (mrdn::help::scSource);
    addAndMakeVisible (scSourceBox);
    scSourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, mrdn::id::scSource, scSourceBox);

    auditionButton.setClickingTogglesState (true);
    auditionButton.setTitle ("Audition sidechain");
    auditionButton.setTooltip (mrdn::help::audition);
    addAndMakeVisible (auditionButton);
    auditionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, mrdn::id::audition, auditionButton);

    triggerLabel.setText ("TRIGGER", juce::dontSendNotification);
    syncOffsetLabel.setText ("OFFSET", juce::dontSendNotification);
    pulseLengthLabel.setText ("LENGTH", juce::dontSendNotification);
    for (auto* l : { &triggerLabel, &syncOffsetLabel, &pulseLengthLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (11.0f)));
        addAndMakeVisible (*l);
    }

    triggerModeBox.addItemList (mrdn::triggerModeChoices, 1);
    triggerModeBox.setTitle ("Trigger mode");
    triggerModeBox.setTooltip (mrdn::help::triggerMode);
    addAndMakeVisible (triggerModeBox);
    triggerModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, mrdn::id::triggerMode, triggerModeBox);

    syncRateBox.addItemList (mrdn::syncRateChoices, 1);
    syncRateBox.setTitle ("Sync rate");
    syncRateBox.setTooltip (mrdn::help::syncRate);
    addAndMakeVisible (syncRateBox);
    syncRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, mrdn::id::syncRate, syncRateBox);

    for (auto* s : { &syncOffsetSlider, &pulseLengthSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setPopupDisplayEnabled (true, true, this);
        addAndMakeVisible (*s);
    }
    syncOffsetSlider.setTitle ("Sync offset");
    pulseLengthSlider.setTitle ("Pulse length");
    syncOffsetSlider.setTooltip (mrdn::help::syncOffset);
    pulseLengthSlider.setTooltip (mrdn::help::pulseLength);
    syncOffsetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, mrdn::id::syncOffset, syncOffsetSlider);
    pulseLengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, mrdn::id::pulseLength, pulseLengthSlider);

    midiLearnTargets[&syncOffsetSlider] = mrdn::id::syncOffset;
    midiLearnTargets[&pulseLengthSlider] = mrdn::id::pulseLength;
    midiLearnTargets[&linkAmountSlider] = mrdn::id::linkAmount;

    addBandButton.setTitle ("Add sidechain EQ band");
    addBandButton.setTooltip (mrdn::help::addBand);
    addBandButton.onClick = [this]
    {
        for (int b = 0; b < mrdn::numScBands; ++b)
        {
            if (! isBandOn (b))
            {
                setBandOn (b, true);
                break;
            }
        }
    };
    addAndMakeVisible (addBandButton);

    for (int b = 0; b < mrdn::numScBands; ++b)
    {
        auto& ui = scBands[(size_t) b];
        const auto bandName = "Sidechain band " + juce::String (b + 1) + " ";

        ui.remove.setButtonText (juce::String::fromUTF8 ("\xC3\x97")); // ×
        ui.remove.setTitle (bandName + "remove");
        ui.remove.setTooltip (mrdn::help::scBandRemove);
        ui.remove.onClick = [this, b] { setBandOn (b, false); };
        addAndMakeVisible (ui.remove);

        ui.type.addItemList (mrdn::scTypeChoices, 1);
        ui.type.setTitle (bandName + "type");
        ui.type.setTooltip (mrdn::help::scBandType);
        addAndMakeVisible (ui.type);
        ui.typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.apvts, mrdn::scBandParamId (b, "type"), ui.type);

        ui.chan.addItemList (mrdn::scChanChoices, 1);
        ui.chan.setTitle (bandName + "channel");
        ui.chan.setTooltip (mrdn::help::scBandChan);
        addAndMakeVisible (ui.chan);
        ui.chanAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.apvts, mrdn::scBandParamId (b, "chan"), ui.chan);

        for (auto* s : { &ui.freq, &ui.gain, &ui.q })
        {
            s->setSliderStyle (juce::Slider::RotaryVerticalDrag);
            s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s->setPopupDisplayEnabled (true, true, this);
            addAndMakeVisible (*s);
        }
        ui.freq.setTitle (bandName + "frequency");
        ui.gain.setTitle (bandName + "gain");
        ui.q.setTitle (bandName + "Q");
        ui.freq.setTooltip (mrdn::help::scBandFreq);
        ui.gain.setTooltip (mrdn::help::scBandGain);
        ui.q.setTooltip (mrdn::help::scBandQ);
        ui.freqAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, mrdn::scBandParamId (b, "freq"), ui.freq);
        ui.gainAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, mrdn::scBandParamId (b, "gain"), ui.gain);
        ui.qAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, mrdn::scBandParamId (b, "q"), ui.q);

        midiLearnTargets[&ui.freq] = mrdn::scBandParamId (b, "freq");
        midiLearnTargets[&ui.gain] = mrdn::scBandParamId (b, "gain");
        midiLearnTargets[&ui.q]    = mrdn::scBandParamId (b, "q");

        ui.freqLabel.setText ("FREQ", juce::dontSendNotification);
        ui.gainLabel.setText ("GAIN", juce::dontSendNotification);
        ui.qLabel.setText ("Q", juce::dontSendNotification);
        for (auto* l : { &ui.freqLabel, &ui.gainLabel, &ui.qLabel })
        {
            l->setFont (juce::Font (juce::FontOptions (9.0f)));
            l->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (*l);
        }
    }

    // Metering ---------------------------------------------------------------
    for (auto* m : { &inMeterLabel, &grMeterLabel, &outMeterLabel })
    {
        m->setFont (juce::Font (juce::FontOptions (11.0f)));
        m->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*m);
    }
    inMeterLabel.setText ("IN", juce::dontSendNotification);
    grMeterLabel.setText ("GR", juce::dontSendNotification);
    outMeterLabel.setText ("OUT", juce::dontSendNotification);

    grReadout.setFont (juce::Font (juce::FontOptions (12.0f)));
    grReadout.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (grReadout);

    lufsReadout.setFont (juce::Font (juce::FontOptions (11.0f)));
    lufsReadout.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (lufsReadout);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (grMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (mainDisplay);

    // Help hints bar + overlays (added last: they sit on top).
    hintBar.setFont (juce::Font (juce::FontOptions (12.0f)));
    hintBar.setJustificationType (juce::Justification::centredLeft);
    if (helpHintsOn)
        hintBar.setText ("Hover any control for help. Press H to hide this bar, "
                         "F1 for shortcuts.", juce::dontSendNotification);
    addChildComponent (hintBar);

    addChildComponent (shortcutsOverlay);

    presetBrowser.onVisibilityChange = [this]
    {
        if (! presetBrowser.isVisible())
            grabKeyboardFocus();
    };
    addChildComponent (presetBrowser);

    applyStaticColours();

    // Open the sidechain panel if the loaded state is already using it.
    sidechainOpen = sidechainInUse();
    sidechainToggle.setToggleState (sidechainOpen, juce::dontSendNotification);

    // Right-clicks on any registered control open the MIDI Learn menu; the
    // same listener feeds the help hints bar on mouse moves.
    addMouseListener (this, true);

    refreshLayout();
    startTimerHz (30);
}

// Explicit component colours in one place so the high-contrast palette swap
// can re-apply them (the LookAndFeel ids are re-broadcast separately).
void MeridianEditorContent::applyStaticColours()
{
    using namespace mrdn;

    title.setColour (juce::Label::textColourId, colors::accent);
    cpuLabel.setColour (juce::Label::textColourId, colors::textDim);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, colors::warn.darker (0.3f));
    undoButton.setColours (colors::textDim, colors::text, colors::accent);
    redoButton.setColours (colors::textDim, colors::text, colors::accent);
    for (auto* b : { &slotAButton, &slotBButton })
        b->setColour (juce::TextButton::buttonOnColourId, colors::accentDim);

    for (auto* l : { &detectorLabel, &oversamplingLabel, &characterLabel, &linkLabel,
                     &scSourceLabel, &triggerLabel, &syncOffsetLabel, &pulseLengthLabel,
                     &inMeterLabel, &grMeterLabel, &outMeterLabel, &lufsReadout })
        l->setColour (juce::Label::textColourId, colors::textDim);

    scEmptyLabel.setColour (juce::Label::textColourId, colors::textDim.withAlpha (0.7f));
    grReadout.setColour (juce::Label::textColourId, colors::gainRed);
    auditionButton.setColour (juce::TextButton::buttonOnColourId,
                              colors::gainRed.darker (0.4f));
    sidechainToggle.setColour (juce::TextButton::textColourOffId,
                               scInUseIndicator ? colors::accent : colors::text);

    for (auto* k : { &threshold, &ratio, &attack, &release, &knee, &makeup, &mix, &range,
                     &hold, &lookahead, &drive, &inTrim, &outTrim })
        k->label.setColour (juce::Label::textColourId, colors::textDim);

    for (auto& ui : scBands)
        for (auto* l : { &ui.freqLabel, &ui.gainLabel, &ui.qLabel })
            l->setColour (juce::Label::textColourId, colors::textDim);

    hintBar.setColour (juce::Label::textColourId, colors::text);
    hintBar.setColour (juce::Label::backgroundColourId, colors::panel.withAlpha (0.6f));
}

// Interaction ------------------------------------------------------------------

void MeridianEditorContent::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    const auto it = midiLearnTargets.find (e.originalComponent);
    if (it == midiLearnTargets.end())
        return;

    const juce::String paramId = it->second;
    const int mappedCc = processor.getMappedCc (paramId);

    juce::PopupMenu menu;
    menu.addItem (1, "MIDI Learn");
    if (mappedCc >= 0)
        menu.addItem (2, "Remove MIDI CC " + juce::String (mappedCc));
    if (processor.isMidiLearnArmed())
        menu.addItem (3, "Cancel MIDI Learn");

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (e.eventComponent),
        [this, paramId] (int result)
        {
            if (result == 1)
                processor.armMidiLearn (paramId);
            else if (result == 2)
                processor.clearMidiMapping (paramId);
            else if (result == 3)
                processor.cancelMidiLearn();
        });
}

void MeridianEditorContent::mouseMove (const juce::MouseEvent& e)
{
    if (! helpHintsOn)
        return;

    juce::String text;
    for (auto* c = e.eventComponent; c != nullptr && c != this; c = c->getParentComponent())
    {
        if (c == &mainDisplay)
        {
            text = mrdn::help::mainDisplayText;
            break;
        }
        if (c == &inMeter || c == &grMeter || c == &outMeter)
        {
            text = mrdn::help::meterStrip;
            break;
        }
        if (auto* client = dynamic_cast<juce::TooltipClient*> (c))
        {
            if (auto tip = client->getTooltip(); tip.isNotEmpty())
            {
                text = tip;
                break;
            }
        }
    }

    hintBar.setText (text, juce::dontSendNotification);
}

bool MeridianEditorContent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    // Letter key codes arrive in platform-dependent case; normalize.
    int code = key.getKeyCode();
    if (code < 256)
        code = (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);

    if (mods.isCommandDown())
    {
        if (code == 'Z' && mods.isShiftDown()) { processor.stateManager.redo(); return true; }
        if (code == 'Z')                       { processor.stateManager.undo(); return true; }
        if (code == 'Y')                       { processor.stateManager.redo(); return true; }
        return false;
    }

    if (key == juce::KeyPress::F1Key)
    {
        shortcutsOverlay.setVisible (! shortcutsOverlay.isVisible());
        shortcutsOverlay.toFront (true);
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        if (shortcutsOverlay.isVisible()) { shortcutsOverlay.setVisible (false); return true; }
        if (presetBrowser.isVisible())    { presetBrowser.closeBrowser(); return true; }
        return false;
    }

    if (mods.isShiftDown())
    {
        if (code == 'B') { toggleBoolParam (mrdn::id::bypass); return true; }
        return false;
    }

    if (mods.isAnyModifierKeyDown())
        return false;

    switch (code)
    {
        case 'A': processor.stateManager.setActiveSlot (0); return true;
        case 'B': processor.stateManager.setActiveSlot (1); return true;
        case 'C': processor.stateManager.copyActiveToOther(); return true;
        case 'P':
            if (presetBrowser.isVisible()) presetBrowser.closeBrowser();
            else                           presetBrowser.openBrowser();
            return true;
        case '[': processor.presets.loadAdjacent (false); return true;
        case ']': processor.presets.loadAdjacent (true); return true;
        case 'H': setHelpHints (! helpHintsOn); return true;
        case 'V':
            advancedToggle.setToggleState (! advancedToggle.getToggleState(),
                                           juce::sendNotification);
            return true;
        case 'S':
            sidechainToggle.setToggleState (! sidechainToggle.getToggleState(),
                                            juce::sendNotification);
            return true;
        default: break;
    }
    return false;
}

void MeridianEditorContent::toggleBoolParam (const juce::String& paramId)
{
    if (auto* param = processor.apvts.getParameter (paramId))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
        param->endChangeGesture();
    }
}

// Help menu ----------------------------------------------------------------------

void MeridianEditorContent::showHelpMenu()
{
    const float scale = lookAndFeel.getTextScale();

    juce::PopupMenu sizes;
    sizes.addItem (10, "100%", true, juce::approximatelyEqual (scale, 1.0f));
    sizes.addItem (11, "115%", true, juce::approximatelyEqual (scale, 1.15f));
    sizes.addItem (12, "130%", true, juce::approximatelyEqual (scale, 1.3f));

    juce::PopupMenu menu;
    menu.addItem (1, "Open Manual...");
    menu.addItem (2, "Keyboard Shortcuts (F1)");
    menu.addSeparator();
    menu.addItem (3, "Help Hints Bar (H)", true, helpHintsOn);
    menu.addItem (4, "High Contrast", true, mrdn::colors::highContrast);
    menu.addSubMenu ("Text Size", sizes);

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (&helpButton),
        [this] (int result)
        {
            switch (result)
            {
                case 1: openManual(); break;
                case 2:
                    shortcutsOverlay.setVisible (true);
                    shortcutsOverlay.toFront (true);
                    break;
                case 3: setHelpHints (! helpHintsOn); break;
                case 4: setHighContrast (! mrdn::colors::highContrast); break;
                case 10: setTextScale (1.0f); break;
                case 11: setTextScale (1.15f); break;
                case 12: setTextScale (1.3f); break;
                default: break;
            }
        });
}

void MeridianEditorContent::openManual()
{
    // The manual ships inside the binary (no installer yet); materialize it
    // next to the settings file and open the default browser.
    auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Equinox Audio").getChildFile ("Meridian Manual.html");
    file.getParentDirectory().createDirectory();
    if (file.replaceWithText (juce::String::fromUTF8 (mrdn::help::manualHtml())))
        juce::URL (file).launchInDefaultBrowser();
}

void MeridianEditorContent::setHelpHints (bool on)
{
    helpHintsOn = on;
    settings.setValue ("help_hints", on);
    hintBar.setText (on ? juce::String ("Hover any control for help. "
                                        "Press H to hide this bar, F1 for shortcuts.")
                        : juce::String(),
                     juce::dontSendNotification);
    refreshLayout();
}

void MeridianEditorContent::setHighContrast (bool on)
{
    mrdn::colors::setHighContrast (on);
    lookAndFeel.applyPalette();
    settings.setValue ("high_contrast", on);
    applyStaticColours();

    if (auto* top = getTopLevelComponent())
    {
        top->sendLookAndFeelChange();
        top->repaint();
    }
}

void MeridianEditorContent::setTextScale (float scale)
{
    lookAndFeel.setTextScale (scale);
    settings.setValue ("text_scale", scale);

    if (auto* top = getTopLevelComponent())
    {
        top->sendLookAndFeelChange();
        top->repaint();
    }
}

void MeridianEditorContent::setupKnob (Knob& k, const juce::String& paramId,
                                       const juce::String& name,
                                       const juce::String& tooltip)
{
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    k.slider.setTitle (name);
    k.slider.setTooltip (tooltip);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setFont (juce::Font (juce::FontOptions (11.0f)));
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, paramId, k.slider);

    midiLearnTargets[&k.slider] = paramId;
}

// Section state ---------------------------------------------------------------

bool MeridianEditorContent::isBandOn (int band) const
{
    return processor.apvts.getRawParameterValue (mrdn::scBandParamId (band, "on"))->load() > 0.5f;
}

void MeridianEditorContent::setBandOn (int band, bool on)
{
    if (auto* param = processor.apvts.getParameter (mrdn::scBandParamId (band, "on")))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

bool MeridianEditorContent::sidechainInUse() const
{
    auto& s = processor.apvts;
    if ((int) s.getRawParameterValue (mrdn::id::scSource)->load() == 1)
        return true;
    if (s.getRawParameterValue (mrdn::id::audition)->load() > 0.5f)
        return true;
    if ((int) s.getRawParameterValue (mrdn::id::triggerMode)->load()
        != (int) mrdn::TriggerMode::audio)
        return true;
    for (int b = 0; b < mrdn::numScBands; ++b)
        if (isBandOn (b))
            return true;
    return false;
}

void MeridianEditorContent::refreshLayout()
{
    advancedToggle.setButtonText (juce::String::fromUTF8 (
        advancedOpen ? "\xe2\x96\xbe ADVANCED" : "\xe2\x96\xb8 ADVANCED"));
    sidechainToggle.setButtonText (juce::String::fromUTF8 (
        sidechainOpen ? "\xe2\x96\xbe SIDECHAIN" : "\xe2\x96\xb8 SIDECHAIN"));

    // Advanced panel visibility.
    for (auto* k : { &hold, &lookahead, &drive, &inTrim, &outTrim })
    {
        k->slider.setVisible (advancedOpen);
        k->label.setVisible (advancedOpen);
    }
    for (auto* c : std::initializer_list<juce::Component*> {
             &characterLabel, &characterBox, &characterPosBox, &autoGainButton,
             &linkLabel, &linkModeBox, &linkAmountSlider })
        c->setVisible (advancedOpen);

    // Sidechain panel visibility: only bands that are switched on get a cell.
    visibleBandMask = 0;
    int bandsOn = 0;
    for (int b = 0; b < mrdn::numScBands; ++b)
    {
        const bool on = isBandOn (b);
        if (on)
        {
            visibleBandMask |= (1u << b);
            ++bandsOn;
        }

        auto& ui = scBands[(size_t) b];
        const bool show = sidechainOpen && on;
        for (auto* c : std::initializer_list<juce::Component*> {
                 &ui.remove, &ui.type, &ui.chan, &ui.freq, &ui.gain, &ui.q,
                 &ui.freqLabel, &ui.gainLabel, &ui.qLabel })
            c->setVisible (show);
    }

    for (auto* c : std::initializer_list<juce::Component*> {
             &scSourceLabel, &scSourceBox, &auditionButton, &addBandButton,
             &triggerLabel, &triggerModeBox })
        c->setVisible (sidechainOpen);
    scEmptyLabel.setVisible (sidechainOpen && bandsOn == 0);
    addBandButton.setEnabled (bandsOn < mrdn::numScBands);

    // Sync settings only matter in Host Sync mode.
    shownTriggerMode = (int) processor.apvts.getRawParameterValue (mrdn::id::triggerMode)->load();
    const bool showSync = sidechainOpen && shownTriggerMode == (int) mrdn::TriggerMode::hostSync;
    for (auto* c : std::initializer_list<juce::Component*> {
             &syncRateBox, &syncOffsetLabel, &syncOffsetSlider,
             &pulseLengthLabel, &pulseLengthSlider })
        c->setVisible (showSync);

    hintBar.setVisible (helpHintsOn);

    int height = headerH + displayH + bigRowH + row2H + toggleBarH + bottomPad;
    if (advancedOpen)
        height += advancedH;
    if (sidechainOpen)
        height += sidechainH;
    if (helpHintsOn)
        height += hintBarH;

    if (getWidth() == editorWidth && getHeight() == height)
        resized();
    else
        setSize (editorWidth, height); // shell resizes itself via childBoundsChanged

    repaint();
}

// Painting & layout -------------------------------------------------------------

void MeridianEditorContent::paint (juce::Graphics& g)
{
    g.fillAll (mrdn::colors::background);

    // Product mark: a disc crossed by its meridian (vertical great circle).
    // Equinox draws the same disc with an equator line — the family's quiet
    // nod to the celestial names, one glyph per plugin.
    if (! logoArea.isEmpty())
    {
        auto disc = logoArea.toFloat().withSizeKeepingCentre (15.0f, 15.0f);
        g.setColour (mrdn::colors::accent);
        g.drawEllipse (disc, 1.2f);
        g.drawEllipse (disc.reduced (disc.getWidth() * 0.325f, 0.0f), 1.0f);
    }

    // Header divider.
    g.setColour (mrdn::colors::outline);
    g.drawHorizontalLine (headerH, 0.0f, (float) getWidth());

    // Meter strip panel.
    g.setColour (mrdn::colors::panel.withAlpha (0.5f));
    g.fillRect (getWidth() - meterStripW, headerH + 1, meterStripW, getHeight() - headerH);
    g.setColour (mrdn::colors::outline);
    g.drawVerticalLine (getWidth() - meterStripW, (float) (headerH + 1), (float) getHeight());

    // Collapsible section panels.
    for (const auto& panel : { advancedPanelArea, sidechainPanelArea })
    {
        if (panel.isEmpty())
            continue;
        g.setColour (mrdn::colors::panel.withAlpha (0.5f));
        g.fillRoundedRectangle (panel.toFloat(), 4.0f);
        g.setColour (mrdn::colors::outline);
        g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }
}

void MeridianEditorContent::resized()
{
    auto area = getLocalBounds();

    // Help hints bar sits under everything to the left of the meter strip.
    if (helpHintsOn)
        hintBar.setBounds (0, getHeight() - hintBarH,
                           getWidth() - meterStripW, hintBarH);

    // Header.
    auto header = area.removeFromTop (headerH).reduced (10, 8);
    logoArea = header.removeFromLeft (22);
    title.setBounds (header.removeFromLeft (104));
    styleBox.setBounds (header.removeFromLeft (120).reduced (0, 2));
    header.removeFromLeft (8);
    bypassButton.setBounds (header.removeFromLeft (64).reduced (0, 2));
    header.removeFromLeft (12);
    slotAButton.setBounds (header.removeFromLeft (24).reduced (0, 2));
    header.removeFromLeft (2);
    slotBButton.setBounds (header.removeFromLeft (24).reduced (0, 2));
    header.removeFromLeft (2);
    copyAbButton.setBounds (header.removeFromLeft (44).reduced (0, 2));
    header.removeFromLeft (12);
    prevPresetButton.setBounds (header.removeFromLeft (20).reduced (0, 2));
    presetNameButton.setBounds (header.removeFromLeft (170).reduced (0, 2));
    nextPresetButton.setBounds (header.removeFromLeft (20).reduced (0, 2));

    helpButton.setBounds (header.removeFromRight (48).reduced (0, 2));
    header.removeFromRight (14);
    redoButton.setBounds (header.removeFromRight (30).reduced (0, 2));
    header.removeFromRight (4);
    undoButton.setBounds (header.removeFromRight (30).reduced (0, 2));
    header.removeFromRight (10);
    cpuLabel.setBounds (header.removeFromRight (64));

    // Meter strip.
    auto meterArea = area.removeFromRight (meterStripW).reduced (14, 10);
    lufsReadout.setBounds (meterArea.removeFromBottom (18));
    grReadout.setBounds (meterArea.removeFromBottom (20));
    auto labels = meterArea.removeFromBottom (16);
    const int meterW = meterArea.getWidth() / 3;

    auto inCol = meterArea.removeFromLeft (meterW);
    auto grCol = meterArea.removeFromLeft (meterW);
    auto outCol = meterArea;
    inMeter.setBounds (inCol.reduced (8, 2));
    grMeter.setBounds (grCol.reduced (8, 2));
    outMeter.setBounds (outCol.reduced (8, 2));
    inMeterLabel.setBounds (labels.removeFromLeft (meterW));
    grMeterLabel.setBounds (labels.removeFromLeft (meterW));
    outMeterLabel.setBounds (labels);

    // Main display: the central view.
    mainDisplay.setBounds (area.removeFromTop (displayH).reduced (14, 6));

    auto layoutKnob = [] (Knob& k, juce::Rectangle<int> cell)
    {
        k.label.setBounds (cell.removeFromTop (16));
        k.slider.setBounds (cell.reduced (2));
    };

    // Big knob row.
    auto bigRow = area.removeFromTop (bigRowH).reduced (14, 4);
    const int bigW = bigRow.getWidth() / 4;
    layoutKnob (threshold, bigRow.removeFromLeft (bigW));
    layoutKnob (ratio,     bigRow.removeFromLeft (bigW));
    layoutKnob (attack,    bigRow.removeFromLeft (bigW));
    layoutKnob (release,   bigRow);

    // Second row: knee/makeup/mix/range + detector column.
    auto row2 = area.removeFromTop (row2H).reduced (14, 4);
    const int colW = row2.getWidth() / 5;
    const int w2 = (row2.getWidth() - colW) / 4;
    layoutKnob (knee,   row2.removeFromLeft (w2));
    layoutKnob (makeup, row2.removeFromLeft (w2));
    layoutKnob (mix,    row2.removeFromLeft (w2));
    layoutKnob (range,  row2.removeFromLeft (w2));

    auto detArea = row2.reduced (6, 0);
    detectorLabel.setBounds (detArea.removeFromTop (12));
    detectorBox.setBounds (detArea.removeFromTop (20));
    detArea.removeFromTop (2);
    oversamplingLabel.setBounds (detArea.removeFromTop (12));
    oversamplingBox.setBounds (detArea.removeFromTop (20));
    detArea.removeFromTop (3);
    auto autoRow = detArea.removeFromTop (20);
    autoReleaseButton.setBounds (autoRow.removeFromLeft (autoRow.getWidth() / 2 - 1));
    autoRow.removeFromLeft (2);
    autoThresholdButton.setBounds (autoRow);

    // Toggle bar.
    auto toggles = area.removeFromTop (toggleBarH).reduced (14, 4);
    advancedToggle.setBounds (toggles.removeFromLeft (110));
    toggles.removeFromLeft (8);
    sidechainToggle.setBounds (toggles.removeFromLeft (110));

    // Advanced panel: utility knobs + character column + link column.
    advancedPanelArea = {};
    if (advancedOpen)
    {
        advancedPanelArea = area.removeFromTop (advancedH).reduced (14, 2);
        auto inner = advancedPanelArea.reduced (8, 6);

        auto linkCol = inner.removeFromRight (150).reduced (4, 0);
        linkLabel.setBounds (linkCol.removeFromTop (12));
        linkModeBox.setBounds (linkCol.removeFromTop (20));
        linkCol.removeFromTop (4);
        linkAmountSlider.setBounds (linkCol.removeFromTop (20));

        auto charCol = inner.removeFromRight (118).reduced (4, 0);
        characterLabel.setBounds (charCol.removeFromTop (12));
        characterBox.setBounds (charCol.removeFromTop (20));
        charCol.removeFromTop (2);
        characterPosBox.setBounds (charCol.removeFromTop (20));
        charCol.removeFromTop (3);
        autoGainButton.setBounds (charCol.removeFromTop (20));

        const int kw = inner.getWidth() / 5;
        layoutKnob (hold,      inner.removeFromLeft (kw));
        layoutKnob (lookahead, inner.removeFromLeft (kw));
        layoutKnob (drive,     inner.removeFromLeft (kw));
        layoutKnob (inTrim,    inner.removeFromLeft (kw));
        layoutKnob (outTrim,   inner);
    }

    // Sidechain panel: source/audition/add row, then one cell per active band.
    sidechainPanelArea = {};
    if (sidechainOpen)
    {
        sidechainPanelArea = area.removeFromTop (sidechainH).reduced (14, 2);
        auto inner = sidechainPanelArea.reduced (8, 6);

        auto top = inner.removeFromTop (22);
        scSourceLabel.setBounds (top.removeFromLeft (52));
        scSourceBox.setBounds (top.removeFromLeft (90));
        top.removeFromLeft (8);
        auditionButton.setBounds (top.removeFromLeft (76));
        addBandButton.setBounds (top.removeFromRight (64));

        inner.removeFromTop (4);

        auto trigRow = inner.removeFromTop (22);
        triggerLabel.setBounds (trigRow.removeFromLeft (52));
        triggerModeBox.setBounds (trigRow.removeFromLeft (90));
        trigRow.removeFromLeft (8);
        syncRateBox.setBounds (trigRow.removeFromLeft (64));
        trigRow.removeFromLeft (8);
        syncOffsetLabel.setBounds (trigRow.removeFromLeft (46));
        syncOffsetSlider.setBounds (trigRow.removeFromLeft (86));
        trigRow.removeFromLeft (8);
        pulseLengthLabel.setBounds (trigRow.removeFromLeft (48));
        pulseLengthSlider.setBounds (trigRow.removeFromLeft (86));

        inner.removeFromTop (4);

        std::vector<int> active;
        for (int b = 0; b < mrdn::numScBands; ++b)
            if (isBandOn (b))
                active.push_back (b);

        if (active.empty())
        {
            scEmptyLabel.setBounds (inner);
        }
        else
        {
            const int n = (int) active.size();
            const int bandW = juce::jmin (120, (inner.getWidth() - (n - 1) * 6) / n);

            for (int b : active)
            {
                auto cell = inner.removeFromLeft (bandW);
                inner.removeFromLeft (6);
                auto& ui = scBands[(size_t) b];

                auto head = cell.removeFromTop (18);
                ui.remove.setBounds (head.removeFromRight (18));
                head.removeFromRight (2);
                ui.type.setBounds (head);
                cell.removeFromTop (2);
                ui.chan.setBounds (cell.removeFromTop (16));
                cell.removeFromTop (2);

                auto knobLabels = cell.removeFromBottom (11);
                const int kw = cell.getWidth() / 3;
                ui.freq.setBounds (cell.removeFromLeft (kw));
                ui.gain.setBounds (cell.removeFromLeft (kw));
                ui.q.setBounds (cell);
                ui.freqLabel.setBounds (knobLabels.removeFromLeft (kw));
                ui.gainLabel.setBounds (knobLabels.removeFromLeft (kw));
                ui.qLabel.setBounds (knobLabels);
            }
        }
    }

    // Overlays cover the whole content.
    presetBrowser.setBounds (getLocalBounds());
    shortcutsOverlay.setBounds (getLocalBounds());
}

void MeridianEditorContent::timerCallback()
{
    // Arm the keyboard shortcuts on open: until focus lands inside the
    // editor, key events die at the top-level window without reaching us.
    // Retries only until the first successful grab (the window may still be
    // in the background on the first ticks), never steals from a control
    // the user has clicked or tabbed into, and never runs again afterwards
    // — a host must be able to keep the keyboard once the user moves on.
    if (! initialFocusGrabbed && isShowing())
    {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused == nullptr || ! (focused == this || isParentOf (focused)))
            grabKeyboardFocus();
        initialFocusGrabbed = hasKeyboardFocus (true);
    }

    inMeter.push (processor.peakIn.exchange (0.0f));
    outMeter.push (processor.peakOut.exchange (0.0f));
    grMeter.push (processor.gainReductionDb.load());

    grReadout.setText (juce::String (grMeter.current(), 1) + " dB",
                       juce::dontSendNotification);

    const int cpuPercent = juce::roundToInt (processor.cpuLoad.load() * 100.0f);
    cpuLabel.setText ("CPU " + juce::String (cpuPercent) + "%", juce::dontSendNotification);
    cpuLabel.setColour (juce::Label::textColourId,
                        cpuPercent > 80 ? mrdn::colors::warn : mrdn::colors::textDim);

    const float lufs = processor.loudness.momentaryLufs.load();
    lufsReadout.setText (lufs <= -99.0f ? juce::String ("M --- LUFS")
                                        : "M " + juce::String (lufs, 1) + " LUFS",
                         juce::dontSendNotification);

    // Persist any MIDI mapping the audio thread captured since last tick.
    processor.commitPendingMidiLearn();

    // Undo/redo/A/B/preset header state follows the state manager.
    auto& sm = processor.stateManager;
    undoButton.setAlpha (sm.canUndo() ? 1.0f : 0.35f);
    redoButton.setAlpha (sm.canRedo() ? 1.0f : 0.35f);

    const int slot = sm.getActiveSlot();
    slotAButton.setToggleState (slot == 0, juce::dontSendNotification);
    slotBButton.setToggleState (slot == 1, juce::dontSendNotification);
    copyAbButton.setButtonText (juce::String::fromUTF8 (
        slot == 0 ? "A\xe2\x86\x92""B" : "B\xe2\x86\x92""A"));

    const auto presetName = sm.getCurrentPresetName();
    const auto buttonText = presetName.isEmpty() ? juce::String ("PRESETS") : presetName;
    if (presetNameButton.getButtonText() != buttonText)
        presetNameButton.setButtonText (buttonText);

    // Band on/off and trigger mode can change from automation, presets, or
    // undo — follow them.
    uint32_t mask = 0;
    for (int b = 0; b < mrdn::numScBands; ++b)
        if (isBandOn (b))
            mask |= (1u << b);
    const int trigMode = (int) processor.apvts.getRawParameterValue (mrdn::id::triggerMode)->load();
    if (mask != visibleBandMask || trigMode != shownTriggerMode)
        refreshLayout();

    // Cue that the collapsed sidechain section is doing something.
    const bool inUse = sidechainInUse();
    if (inUse != scInUseIndicator)
    {
        scInUseIndicator = inUse;
        sidechainToggle.setColour (juce::TextButton::textColourOffId,
                                   inUse ? mrdn::colors::accent : mrdn::colors::text);
    }
}

// Shortcuts overlay ---------------------------------------------------------------

void MeridianEditorContent::ShortcutsOverlay::paint (juce::Graphics& g)
{
    using namespace mrdn;

    g.fillAll (colors::background.withAlpha (0.82f));

    constexpr int rowH = 22;
    const int numRows = (int) std::size (help::shortcuts);
    auto panel = getLocalBounds().withSizeKeepingCentre (
        440, numRows * rowH + 58).toFloat();

    g.setColour (colors::panel);
    g.fillRoundedRectangle (panel, 6.0f);
    g.setColour (colors::outline);
    g.drawRoundedRectangle (panel.reduced (0.5f), 6.0f, 1.0f);

    auto inner = panel.reduced (16.0f, 12.0f);
    g.setColour (colors::text);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText ("KEYBOARD SHORTCUTS", inner.removeFromTop (26.0f),
                juce::Justification::centredLeft);

    for (const auto& s : help::shortcuts)
    {
        auto row = inner.removeFromTop ((float) rowH);
        g.setColour (colors::accent);
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        g.drawText (s.keys, row.removeFromLeft (150.0f), juce::Justification::centredLeft);
        g.setColour (colors::text);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText (s.action, row, juce::Justification::centredLeft);
    }

    g.setColour (colors::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("click anywhere or press any key to close",
                panel.reduced (16.0f, 6.0f).removeFromBottom (14.0f),
                juce::Justification::centredRight);
}

//==============================================================================
// Shell

MeridianAudioProcessorEditor::MeridianAudioProcessorEditor (MeridianAudioProcessor& p)
    : AudioProcessorEditor (p), settings (makeSettingsOptions()),
      content (p, settings, lookAndFeel)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    // Sliders cache their text-box colours when created; re-broadcast the
    // look-and-feel now that the whole tree is attached.
    sendLookAndFeelChange();

    float stored = (float) settings.getDoubleValue ("ui_scale", 0.0);
    if (stored <= 0.0f && settings.containsKey ("ui_size"))
    {
        stored = legacyUiScales[juce::jlimit (0, (int) std::size (legacyUiScales) - 1,
                                              settings.getIntValue ("ui_size", 2))];
        settings.removeValue ("ui_size");
    }
    uiScale = stored > 0.0f ? juce::jlimit (minUiScale, maxUiScale, stored) : 1.0f;

    // Freely resizable: hosts that support it let the user drag any window
    // edge; the bottom-right corner handle is always there as a fallback.
    setResizable (true, true);
    updateSizeConstraints();
    content.setTransform (juce::AffineTransform::scale (uiScale));
    setSize (juce::roundToInt ((float) content.getWidth() * uiScale),
             juce::roundToInt ((float) content.getHeight() * uiScale));

    // GPU-composited painting for the whole UI (metering + animation).
    // Repaints stay timer-driven; the context only redraws dirty regions.
    openGLContext.setContinuousRepainting (false);
    openGLContext.attachTo (*this);
}

MeridianAudioProcessorEditor::~MeridianAudioProcessorEditor()
{
    settings.saveIfNeeded();
    openGLContext.detach();
    setLookAndFeel (nullptr);
}

// Pin the resize constraints to the content's logical shape: the aspect
// ratio tracks whichever panels are open, and the min/max sizes bound the
// scale rather than the pixels.
void MeridianAudioProcessorEditor::updateSizeConstraints()
{
    const auto cw = (float) content.getWidth();
    const auto ch = (float) content.getHeight();
    if (cw <= 0.0f || ch <= 0.0f)
        return;

    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio (cw / ch);
        c->setSizeLimits (juce::roundToInt (cw * minUiScale),
                          juce::roundToInt (ch * minUiScale),
                          juce::roundToInt (cw * maxUiScale),
                          juce::roundToInt (ch * maxUiScale));
    }
}

void MeridianAudioProcessorEditor::resized()
{
    // Our own setSize calls (panel open/close at a kept scale) don't move
    // the scale; only a user/host drag does.
    if (followingContentResize || content.getWidth() <= 0)
        return;

    uiScale = juce::jlimit (minUiScale, maxUiScale,
                            (float) getWidth() / (float) content.getWidth());
    content.setTransform (juce::AffineTransform::scale (uiScale));
    settings.setValue ("ui_scale", uiScale); // written to disk by saveIfNeeded/dtor
}

void MeridianAudioProcessorEditor::childBoundsChanged (juce::Component* child)
{
    if (child != &content)
        return;

    // The content grew or shrank (a panel opened/closed): keep the current
    // scale and re-shape the window to match.
    updateSizeConstraints();

    followingContentResize = true;
    setSize (juce::roundToInt ((float) content.getWidth() * uiScale),
             juce::roundToInt ((float) content.getHeight() * uiScale));
    followingContentResize = false;
}
