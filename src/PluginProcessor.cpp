#include "PluginProcessor.h"
#include "PluginEditor.h"

MeridianAudioProcessor::MeridianAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "MeridianState", mrdn::createParameterLayout())
{
    // Undo/redo, A/B slots, and preset loads all reapply whole state trees;
    // the CC lookup is a cache built from the tree, so refresh it each time.
    stateManager.onStateReapplied = [this] { rebuildMidiCcMap(); };

    thresholdParam = apvts.getRawParameterValue (mrdn::id::threshold);
    ratioParam     = apvts.getRawParameterValue (mrdn::id::ratio);
    attackParam    = apvts.getRawParameterValue (mrdn::id::attack);
    releaseParam   = apvts.getRawParameterValue (mrdn::id::release);
    kneeParam      = apvts.getRawParameterValue (mrdn::id::knee);
    makeupParam    = apvts.getRawParameterValue (mrdn::id::makeup);
    inTrimParam    = apvts.getRawParameterValue (mrdn::id::inTrim);
    outTrimParam   = apvts.getRawParameterValue (mrdn::id::outTrim);
    detectorParam  = apvts.getRawParameterValue (mrdn::id::detector);
    styleParam     = apvts.getRawParameterValue (mrdn::id::style);
    linkAmountParam = apvts.getRawParameterValue (mrdn::id::linkAmount);
    linkModeParam   = apvts.getRawParameterValue (mrdn::id::linkMode);
    scSourceParam   = apvts.getRawParameterValue (mrdn::id::scSource);
    auditionParam   = apvts.getRawParameterValue (mrdn::id::audition);
    bypassParam    = apvts.getRawParameterValue (mrdn::id::bypass);

    for (int b = 0; b < mrdn::numScBands; ++b)
    {
        auto& sc = scBandParams[(size_t) b];
        sc.on   = apvts.getRawParameterValue (mrdn::scBandParamId (b, "on"));
        sc.type = apvts.getRawParameterValue (mrdn::scBandParamId (b, "type"));
        sc.freq = apvts.getRawParameterValue (mrdn::scBandParamId (b, "freq"));
        sc.gain = apvts.getRawParameterValue (mrdn::scBandParamId (b, "gain"));
        sc.q    = apvts.getRawParameterValue (mrdn::scBandParamId (b, "q"));
        sc.chan = apvts.getRawParameterValue (mrdn::scBandParamId (b, "chan"));
    }
    autoReleaseParam   = apvts.getRawParameterValue (mrdn::id::autoRelease);
    autoThresholdParam = apvts.getRawParameterValue (mrdn::id::autoThreshold);
    oversamplingParam  = apvts.getRawParameterValue (mrdn::id::oversampling);
    characterParam     = apvts.getRawParameterValue (mrdn::id::character);
    characterPosParam  = apvts.getRawParameterValue (mrdn::id::characterPos);
    driveParam         = apvts.getRawParameterValue (mrdn::id::drive);
    mixParam           = apvts.getRawParameterValue (mrdn::id::mix);
    rangeParam         = apvts.getRawParameterValue (mrdn::id::range);
    holdParam          = apvts.getRawParameterValue (mrdn::id::hold);
    lookaheadParam     = apvts.getRawParameterValue (mrdn::id::lookahead);
    autoGainParam      = apvts.getRawParameterValue (mrdn::id::autoGain);
    triggerModeParam   = apvts.getRawParameterValue (mrdn::id::triggerMode);
    syncRateParam      = apvts.getRawParameterValue (mrdn::id::syncRate);
    syncOffsetParam    = apvts.getRawParameterValue (mrdn::id::syncOffset);
    pulseLengthParam   = apvts.getRawParameterValue (mrdn::id::pulseLength);

    rebuildMidiCcMap();
}

bool MeridianAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        return sc.isDisabled() || sc == juce::AudioChannelSet::mono()
               || sc == juce::AudioChannelSet::stereo();
    }

    return true;
}

void MeridianAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    const auto numChannels = (juce::uint32) juce::jmax (1, getMainBusNumInputChannels());

    for (size_t i = 1; i < oversamplers.size(); ++i)
    {
        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (
            numChannels, i,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversamplers[i]->initProcessing ((size_t) samplesPerBlock);

        detOversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (
            numChannels, i,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        detOversamplers[i]->initProcessing ((size_t) samplesPerBlock);
    }

    scEq.prepare (sampleRate);
    detBuffer.setSize (2, samplesPerBlock);
    loudness.prepare (sampleRate, getMainBusNumInputChannels());
    scopeAccIn = scopeAccOut = scopeAccGr = scopeAccSc = 0.0f;
    scopeCellPos = 0;
    heldNotes = 0;
    midiGate = 0.0f;

    const double trimRampSeconds = 0.02;
    inTrimSmooth.reset (sampleRate, trimRampSeconds);
    outTrimSmooth.reset (sampleRate, trimRampSeconds);
    bypassMix.reset (sampleRate, 0.01);
    autoGainSmooth.reset (sampleRate, 0.05);

    inTrimSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inTrimParam->load()));
    outTrimSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outTrimParam->load()));
    bypassMix.setCurrentAndTargetValue (bypassParam->load() > 0.5f ? 1.0f : 0.0f);
    autoGainSmooth.setCurrentAndTargetValue (1.0f);
    autoGainTrackDb = 0.0f;

    // Smart parameter interpolation (~20 ms ramps, advanced per block).
    for (auto* s : { &thresholdSmooth, &ratioSmooth, &kneeSmooth, &makeupSmooth,
                     &mixSmooth, &rangeSmooth, &driveSmooth })
        s->reset (sampleRate, trimRampSeconds);

    thresholdSmooth.setCurrentAndTargetValue (thresholdParam->load());
    ratioSmooth.setCurrentAndTargetValue (ratioParam->load());
    kneeSmooth.setCurrentAndTargetValue (kneeParam->load());
    makeupSmooth.setCurrentAndTargetValue (makeupParam->load());
    mixSmooth.setCurrentAndTargetValue (mixParam->load());
    rangeSmooth.setCurrentAndTargetValue (rangeParam->load());
    driveSmooth.setCurrentAndTargetValue (driveParam->load());

    dryBuffer.setSize (2, samplesPerBlock);

    currentOsChoice = -1; // force re-prepare of the engine + latency
    applyOversamplingChoice ((int) oversamplingParam->load());
}

void MeridianAudioProcessor::applyOversamplingChoice (int choice)
{
    choice = juce::jlimit (0, (int) oversamplers.size() - 1, choice);
    if (choice == currentOsChoice)
        return;

    currentOsChoice = choice;
    const int factor = mrdn::oversamplingFactors[choice];
    engine.prepare (hostSampleRate * factor, getMainBusNumInputChannels());
    character.prepare (hostSampleRate * factor, getMainBusNumInputChannels());

    if (choice > 0)
    {
        oversamplers[(size_t) choice]->reset();
        detOversamplers[(size_t) choice]->reset();
    }

    updateLatency();
}

void MeridianAudioProcessor::updateLatency()
{
    const int osLatency = currentOsChoice <= 0
        ? 0
        : juce::roundToInt (oversamplers[(size_t) currentOsChoice]->getLatencyInSamples());

    // Engine lookahead runs at the oversampled rate; convert to host samples.
    const int factor = mrdn::oversamplingFactors[juce::jmax (0, currentOsChoice)];
    const int lookahead = juce::roundToInt (
        (double) engine.getLookaheadSamples() / (double) factor);

    const int total = osLatency + lookahead;
    if (total != lastReportedLatency)
    {
        lastReportedLatency = total;
        setLatencySamples (total);
    }
}

void MeridianAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto blockStartTicks = juce::Time::getHighResolutionTicks();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (getMainBusNumOutputChannels(),
                                        buffer.getNumChannels(), 2);

    if (numSamples == 0 || numChannels == 0)
        return;

    handleMidi (midi); // MIDI Learn capture + mapped CC -> parameter

    applyOversamplingChoice ((int) oversamplingParam->load());

    // Smart parameter interpolation: ramp the audible static parameters.
    thresholdSmooth.setTargetValue (thresholdParam->load());
    ratioSmooth.setTargetValue (ratioParam->load());
    kneeSmooth.setTargetValue (kneeParam->load());
    makeupSmooth.setTargetValue (makeupParam->load());
    mixSmooth.setTargetValue (mixParam->load());
    rangeSmooth.setTargetValue (rangeParam->load());
    driveSmooth.setTargetValue (driveParam->load());

    mrdn::CompressorEngine::Params p;
    p.thresholdDb = thresholdSmooth.skip (numSamples);
    p.ratio       = ratioSmooth.skip (numSamples);
    p.attackMs    = attackParam->load();
    p.releaseMs   = releaseParam->load();
    p.kneeDb      = kneeSmooth.skip (numSamples);
    p.makeupDb    = makeupSmooth.skip (numSamples);
    const auto linkMode = numChannels > 1 ? (mrdn::LinkMode) (int) linkModeParam->load()
                                          : mrdn::LinkMode::stereo;

    p.detector    = (mrdn::DetectorMode) (int) detectorParam->load();
    p.style       = (mrdn::Style) (int) styleParam->load();
    p.linkAmount01 = linkAmountParam->load() * 0.01f;
    p.autoRelease   = autoReleaseParam->load() > 0.5f;
    p.autoThreshold = autoThresholdParam->load() > 0.5f;
    p.mixPercent  = mixSmooth.skip (numSamples);
    p.rangeDb     = rangeSmooth.skip (numSamples);
    p.holdMs      = holdParam->load();
    p.lookaheadMs = lookaheadParam->load();

    switch (linkMode)
    {
        case mrdn::LinkMode::stereo:                                 p.msTarget = -1; break;
        case mrdn::LinkMode::midOnly:   case mrdn::LinkMode::sideToMid: p.msTarget = 0; break;
        case mrdn::LinkMode::sideOnly:  case mrdn::LinkMode::midToSide: p.msTarget = 1; break;
    }

    engine.setParams (p);
    updateLatency();

    std::array<mrdn::SidechainEq::Band, mrdn::numScBands> scBands;
    for (int b = 0; b < mrdn::numScBands; ++b)
    {
        auto& band = scBands[(size_t) b];
        const auto& raw = scBandParams[(size_t) b];
        band.on = raw.on->load() > 0.5f;
        band.type = (mrdn::ScFilterType) (int) raw.type->load();
        band.freq = raw.freq->load();
        band.gainDb = raw.gain->load();
        band.q = raw.q->load();
        band.chan = (int) raw.chan->load();
    }
    scEq.setParams (scBands);

    character.setParams ((mrdn::CharacterMode) (int) characterParam->load(),
                         driveSmooth.skip (numSamples));
    const bool characterPre = (int) characterPosParam->load() == 0;

    inTrimSmooth.setTargetValue (juce::Decibels::decibelsToGain (inTrimParam->load()));
    outTrimSmooth.setTargetValue (juce::Decibels::decibelsToGain (outTrimParam->load()));
    bypassMix.setTargetValue (bypassParam->load() > 0.5f ? 1.0f : 0.0f);

    // Keep the dry signal around for the click-free bypass crossfade.
    dryBuffer.setSize (numChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // Input trim.
    float inPk = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inTrimSmooth.getNextValue();
        left[i] *= g;
        inPk = juce::jmax (inPk, std::abs (left[i]));
        if (right != nullptr)
        {
            right[i] *= g;
            inPk = juce::jmax (inPk, std::abs (right[i]));
        }
    }

    // --- Detector path (host rate). Audio mode: external sidechain or the
    // trimmed input, EQ'd in the M/S domain, then mapped per the link mode.
    // Trigger modes replace the detector with a 0 dBFS gate signal (host-
    // tempo pulse train or MIDI note gate); the SC EQ is bypassed there. ---
    detBuffer.setSize (numChannels, numSamples, false, false, true);

    const auto trigMode = (mrdn::TriggerMode) (int) triggerModeParam->load();
    float* detM = detBuffer.getWritePointer (0);
    float* detS = numChannels > 1 ? detBuffer.getWritePointer (1) : nullptr;

    auto toMidSide = [numSamples] (float* a, float* b)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (a[i] + b[i]), s = 0.5f * (a[i] - b[i]);
            a[i] = m;
            b[i] = s;
        }
    };
    auto fromMidSide = [numSamples] (float* a, float* b)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = a[i] + b[i], r = a[i] - b[i];
            a[i] = l;
            b[i] = r;
        }
    };

    bool haveExternal = false;

    if (trigMode == mrdn::TriggerMode::hostSync)
    {
        // Tempo-locked pulse train: high for pulseLength% of each division,
        // phase-shifted by syncOffset%. Sample-accurate from the host's ppq.
        // Silent (no triggering) when the transport is stopped.
        bool filled = false;
        if (auto* head = getPlayHead())
        {
            if (auto pos = head->getPosition())
            {
                if (pos->getIsPlaying() && pos->getBpm().hasValue()
                    && pos->getPpqPosition().hasValue())
                {
                    const int rate = juce::jlimit (0, mrdn::syncRateChoices.size() - 1,
                                                   (int) syncRateParam->load());
                    const double interval = mrdn::syncRateQuarters[rate];
                    const double ppqPerSample = *pos->getBpm() / (60.0 * hostSampleRate);
                    const double offsetFrac = (double) syncOffsetParam->load() * 0.01;
                    const double pulseFrac = (double) pulseLengthParam->load() * 0.01;

                    double ppq = *pos->getPpqPosition();
                    for (int i = 0; i < numSamples; ++i)
                    {
                        double phase = ppq / interval - offsetFrac;
                        phase -= std::floor (phase);
                        const float v = phase < pulseFrac ? 1.0f : 0.0f;
                        detM[i] = v;
                        if (detS != nullptr)
                            detS[i] = v;
                        ppq += ppqPerSample;
                    }
                    filled = true;
                }
            }
        }

        if (! filled)
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::clear (detBuffer.getWritePointer (ch),
                                                    numSamples);
    }
    else if (trigMode == mrdn::TriggerMode::midi)
    {
        // Note gate, applied at each event's exact sample position; stays
        // high while any note is held.
        int pos = 0;
        for (const auto meta : midi)
        {
            const int at = juce::jlimit (0, numSamples, (int) meta.samplePosition);
            for (int i = pos; i < at; ++i)
            {
                detM[i] = midiGate;
                if (detS != nullptr)
                    detS[i] = midiGate;
            }
            pos = at;

            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
                ++heldNotes;
            else if (msg.isNoteOff())
                heldNotes = juce::jmax (0, heldNotes - 1);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                heldNotes = 0;
            midiGate = heldNotes > 0 ? 1.0f : 0.0f;
        }
        for (int i = pos; i < numSamples; ++i)
        {
            detM[i] = midiGate;
            if (detS != nullptr)
                detS[i] = midiGate;
        }
    }
    else // TriggerMode::audio
    {
        if ((int) scSourceParam->load() == 1 && getBusCount (true) > 1
            && getBus (true, 1)->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0)
            {
                haveExternal = true;
                for (int ch = 0; ch < numChannels; ++ch)
                    detBuffer.copyFrom (ch, 0, sc,
                                        juce::jmin (ch, sc.getNumChannels() - 1),
                                        0, numSamples);
            }
        }

        if (! haveExternal)
            for (int ch = 0; ch < numChannels; ++ch)
                detBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        if (detS != nullptr)
            toMidSide (detM, detS);
        scEq.process (detM, detS, numSamples);
        if (detS != nullptr && linkMode == mrdn::LinkMode::stereo)
            fromMidSide (detM, detS); // stereo mode detects in L/R
    }
    sidechainActive.store (haveExternal);

    float blockGrDb = 0.0f;

    if (auditionParam->load() > 0.5f)
    {
        // Audition: monitor the detector signal instead of the processed
        // audio. In audio mode with an M/S link the detector is in the M/S
        // domain and needs decoding; trigger pulses are already per-channel.
        if (detS != nullptr && linkMode != mrdn::LinkMode::stereo
            && trigMode == mrdn::TriggerMode::audio)
            fromMidSide (detM, detS);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom (ch, 0, detBuffer, ch, 0, numSamples);
    }
    else
    {
        // Audio into the M/S domain when an M/S link mode is active.
        const bool msMode = linkMode != mrdn::LinkMode::stereo && numChannels > 1;
        if (msMode)
            toMidSide (left, right);

        // Which detector channel drives the envelope in M/S modes.
        const bool detFromSide = linkMode == mrdn::LinkMode::sideOnly
                                 || linkMode == mrdn::LinkMode::sideToMid;

        auto runChain = [&] (float* l, float* r, const float* dl, const float* dr, int n)
        {
            if (characterPre)
                character.process (l, r, n);
            const float gr = engine.process (l, r, dl, dr, n);
            if (! characterPre)
                character.process (l, r, n);
            return gr;
        };

        if (currentOsChoice > 0)
        {
            auto& os = *oversamplers[(size_t) currentOsChoice];
            auto& detOs = *detOversamplers[(size_t) currentOsChoice];

            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(),
                                                (size_t) numChannels, (size_t) numSamples);
            auto osBlock = os.processSamplesUp (block);

            juce::dsp::AudioBlock<float> detBlock (detBuffer.getArrayOfWritePointers(),
                                                   (size_t) numChannels, (size_t) numSamples);
            auto osDet = detOs.processSamplesUp (detBlock);

            const float* dl = osDet.getChannelPointer (detFromSide && numChannels > 1 ? 1 : 0);
            const float* dr = numChannels > 1 ? osDet.getChannelPointer (1) : nullptr;

            blockGrDb = runChain (osBlock.getChannelPointer (0),
                                  numChannels > 1 ? osBlock.getChannelPointer (1) : nullptr,
                                  dl, dr, (int) osBlock.getNumSamples());
            os.processSamplesDown (block);
        }
        else
        {
            const float* dl = detFromSide && detS != nullptr ? detS : detM;
            blockGrDb = runChain (left, right, dl, detS, numSamples);
        }

        if (msMode)
            fromMidSide (left, right);
    }

    // Intelligent Auto Gain: slowly track the applied GR and compensate.
    {
        const float target = autoGainParam->load() > 0.5f ? blockGrDb : 0.0f;
        const float coeff = std::exp ((float) (-(double) numSamples
                                               / (hostSampleRate * 2.0))); // ~2 s
        autoGainTrackDb = coeff * autoGainTrackDb + (1.0f - coeff) * target;
        autoGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (autoGainTrackDb));
    }

    // Output trim + auto gain + bypass crossfade back to the dry signal,
    // plus scope-cell accumulation for the main display.
    const float inTrimNow = inTrimSmooth.getCurrentValue();
    float outPk = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outTrimSmooth.getNextValue() * autoGainSmooth.getNextValue();
        const float dry = bypassMix.getNextValue();
        const float wet = 1.0f - dry;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = ch == 0 ? left : right;
            data[i] = data[i] * g * wet + dryBuffer.getSample (ch, i) * dry;
            outPk = juce::jmax (outPk, std::abs (data[i]));

            scopeAccIn = juce::jmax (scopeAccIn,
                                     std::abs (dryBuffer.getSample (ch, i)) * inTrimNow);
            scopeAccOut = juce::jmax (scopeAccOut, std::abs (data[i]));
        }

        scopeAccGr = juce::jmax (scopeAccGr, blockGrDb);
        scopeAccSc = juce::jmax (scopeAccSc, std::abs (detM[i]));
        if (detS != nullptr)
            scopeAccSc = juce::jmax (scopeAccSc, std::abs (detS[i]));

        if (++scopeCellPos >= scopeCellSamples)
        {
            const auto idx = scopeWriteIndex.load (std::memory_order_relaxed);
            scopeRing[idx & (uint32_t) (scopeSize - 1)] = { scopeAccIn, scopeAccOut, scopeAccGr, scopeAccSc };
            scopeWriteIndex.store (idx + 1, std::memory_order_release);
            scopeAccIn = scopeAccOut = scopeAccGr = scopeAccSc = 0.0f;
            scopeCellPos = 0;
        }
    }

    loudness.process (left, right, numSamples);

    gainReductionDb.store (blockGrDb);

    // Peak-hold for the GUI (released when the GUI reads with exchange (0)).
    float prev = peakIn.load();
    while (inPk > prev && ! peakIn.compare_exchange_weak (prev, inPk)) {}
    prev = peakOut.load();
    while (outPk > prev && ! peakOut.compare_exchange_weak (prev, outPk)) {}

    // Smoothed fraction-of-realtime CPU estimate for the GUI.
    const double elapsed = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - blockStartTicks);
    const double budget = (double) numSamples / hostSampleRate;
    const float load = (float) (elapsed / juce::jmax (budget, 1.0e-9));
    cpuLoad.store (0.9f * cpuLoad.load() + 0.1f * load);
}

// MIDI Learn --------------------------------------------------------------

static const juce::Identifier midiMapType ("MIDIMAP");
static const juce::Identifier midiMapEntryType ("MAP");
static const juce::Identifier midiMapCcProp ("cc");
static const juce::Identifier midiMapParamProp ("param");

void MeridianAudioProcessor::handleMidi (juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (! msg.isController())
            continue;

        const int cc = msg.getControllerNumber();
        if (cc < 0 || cc >= (int) ccTargets.size())
            continue;

        // Armed learn: capture this CC. The lookup entry is live at once;
        // commitPendingMidiLearn() persists it on the message thread.
        if (auto* target = learnTarget.exchange (nullptr))
        {
            ccTargets[(size_t) cc].store (target);
            pendingLearnParam.store (target, std::memory_order_relaxed);
            pendingLearnCc.store (cc, std::memory_order_release);
        }

        if (auto* mapped = ccTargets[(size_t) cc].load())
            mapped->setValueNotifyingHost ((float) msg.getControllerValue() / 127.0f);
    }
}

void MeridianAudioProcessor::armMidiLearn (const juce::String& paramId)
{
    learnTarget.store (apvts.getParameter (paramId));
}

int MeridianAudioProcessor::getMappedCc (const juce::String& paramId) const
{
    auto map = apvts.state.getChildWithName (midiMapType);
    for (int i = 0; i < map.getNumChildren(); ++i)
        if (map.getChild (i)[midiMapParamProp].toString() == paramId)
            return map.getChild (i)[midiMapCcProp];
    return -1;
}

void MeridianAudioProcessor::clearMidiMapping (const juce::String& paramId)
{
    auto map = apvts.state.getChildWithName (midiMapType);
    for (int i = map.getNumChildren(); --i >= 0;)
        if (map.getChild (i)[midiMapParamProp].toString() == paramId)
            map.removeChild (i, nullptr);
    rebuildMidiCcMap();
}

void MeridianAudioProcessor::commitPendingMidiLearn()
{
    const int cc = pendingLearnCc.exchange (-1, std::memory_order_acquire);
    if (cc < 0)
        return;

    auto* param = pendingLearnParam.load (std::memory_order_relaxed);
    if (param == nullptr)
        return;

    auto map = apvts.state.getOrCreateChildWithName (midiMapType, nullptr);

    // One CC per parameter and one parameter per CC: drop colliding entries.
    for (int i = map.getNumChildren(); --i >= 0;)
    {
        auto entry = map.getChild (i);
        if ((int) entry[midiMapCcProp] == cc
            || entry[midiMapParamProp].toString() == param->paramID)
            map.removeChild (i, nullptr);
    }

    juce::ValueTree entry (midiMapEntryType);
    entry.setProperty (midiMapCcProp, cc, nullptr);
    entry.setProperty (midiMapParamProp, param->paramID, nullptr);
    map.appendChild (entry, nullptr);

    rebuildMidiCcMap();

    // The mapping changed the state tree without touching a parameter —
    // tell the history so undo and session snapshots see it.
    stateManager.noteNonParameterChange();
}

void MeridianAudioProcessor::rebuildMidiCcMap()
{
    for (auto& t : ccTargets)
        t.store (nullptr);

    auto map = apvts.state.getChildWithName (midiMapType);
    for (int i = 0; i < map.getNumChildren(); ++i)
    {
        auto entry = map.getChild (i);
        const int cc = entry[midiMapCcProp];
        if (cc >= 0 && cc < (int) ccTargets.size())
            ccTargets[(size_t) cc].store (
                apvts.getParameter (entry[midiMapParamProp].toString()));
    }
}

// State ---------------------------------------------------------------------

void MeridianAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    commitPendingMidiLearn(); // runs on the message thread; flush before saving

    if (auto xml = stateManager.saveSession().createXml())
    {
        xml->setAttribute ("stateVersion", stateVersion);
        copyXmlToBinary (*xml, destData);
    }
}

bool MeridianAudioProcessor::applyStateXml (const juce::XmlElement& xml)
{
    if (xml.getIntAttribute ("stateVersion", 0) > stateVersion)
        return false; // written by a newer schema than this build understands

    // v2 session: both A/B slots + which is active.
    if (xml.hasTagName (stateManager.sessionType.toString()))
        return stateManager.loadSession (juce::ValueTree::fromXml (xml));

    // v1 session or bare preset state: one state, seeded into both slots.
    if (xml.hasTagName (apvts.state.getType()))
    {
        stateManager.loadLegacyState (juce::ValueTree::fromXml (xml));
        return true;
    }

    return false;
}

void MeridianAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateXml (*xml);
}

juce::AudioProcessorEditor* MeridianAudioProcessor::createEditor()
{
    return new MeridianAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MeridianAudioProcessor();
}
