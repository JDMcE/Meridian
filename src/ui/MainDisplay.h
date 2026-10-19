#pragma once

#include "../PluginProcessor.h"
#include "LookAndFeel.h"
#include <deque>

namespace mrdn
{

// The central view: a scrolling level history (input dim, output cyan,
// applied GR in amber from the top) with an animated transfer-curve panel
// overlaid top-right — the curve follows the current style/threshold/ratio/
// knee, and the operating point rides it with the programme.
// (GPU acceleration + resizable sizes land in the second half of v0.6.)
class MainDisplay : public juce::Component,
                    private juce::Timer
{
public:
    explicit MainDisplay (MeridianAudioProcessor& p) : processor (p)
    {
        curveEngine.prepare (48000.0, 2);
        lastReadIndex = processor.scopeWriteIndex.load();
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (colors::panel);
        g.fillRoundedRectangle (area, 4.0f);

        drawGrid (g, area);
        drawHistory (g, area);
        drawControlOverlays (g, area);
        drawKneePanel (g);

        g.setColour (colors::outline);
        g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);
    }

private:
    static constexpr float minDb = -60.0f;
    static constexpr float grScaleDb = 24.0f; // GR band uses the top 40 %

    float dbToY (float db, juce::Rectangle<float> area) const
    {
        const float norm = juce::jlimit (0.0f, 1.0f, (db - minDb) / -minDb);
        return area.getBottom() - norm * area.getHeight();
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (int db = -12; db > (int) minDb; db -= 12)
        {
            const float y = dbToY ((float) db, area);
            g.setColour (colors::outline.withAlpha (0.5f));
            g.drawHorizontalLine ((int) y, area.getX() + 2.0f, area.getRight() - 2.0f);
            g.setColour (colors::textDim.withAlpha (0.8f));
            g.drawText (juce::String (db), (int) area.getX() + 4, (int) y - 12, 30, 12,
                        juce::Justification::left);
        }
    }

    void drawHistory (juce::Graphics& g, juce::Rectangle<float> area)
    {
        if (history.empty())
            return;

        const float cellW = 2.0f;
        const int visible = juce::jmin ((int) history.size(),
                                        (int) std::ceil (area.getWidth() / cellW));

        juce::Path inPath, outPath, grPath, scPath;
        const float x0 = area.getRight() - (float) visible * cellW;

        for (int i = 0; i < visible; ++i)
        {
            const auto& f = history[history.size() - (size_t) visible + (size_t) i];
            const float x = x0 + (float) i * cellW;

            const float yIn = dbToY (juce::Decibels::gainToDecibels (f.inPk, minDb), area);
            const float yOut = dbToY (juce::Decibels::gainToDecibels (f.outPk, minDb), area);
            const float ySc = dbToY (juce::Decibels::gainToDecibels (f.scPk, minDb), area);
            const float grH = juce::jlimit (0.0f, 1.0f, f.grDb / grScaleDb)
                              * area.getHeight() * 0.4f;

            if (i == 0)
            {
                inPath.startNewSubPath (x, yIn);
                outPath.startNewSubPath (x, yOut);
                scPath.startNewSubPath (x, ySc);
                grPath.startNewSubPath (x, area.getY() + grH);
            }
            else
            {
                inPath.lineTo (x, yIn);
                outPath.lineTo (x, yOut);
                scPath.lineTo (x, ySc);
                grPath.lineTo (x, area.getY() + grH);
            }
        }

        // Input: dim filled area.
        auto closeDown = [&area, x0] (juce::Path p)
        {
            p.lineTo (area.getRight(), area.getBottom());
            p.lineTo (x0, area.getBottom());
            p.closeSubPath();
            return p;
        };

        g.setColour (colors::textDim.withAlpha (0.18f));
        g.fillPath (closeDown (inPath));

        // External sidechain: the post-EQ detector signal, drawn under the
        // output so the trigger and its effect read together.
        if (scTraceVisible)
        {
            g.setColour (colors::sidechain.withAlpha (0.9f));
            g.strokePath (scPath, juce::PathStrokeType (1.2f));
        }

        g.setColour (colors::accent.withAlpha (0.28f));
        g.fillPath (closeDown (outPath));
        g.setColour (colors::accent);
        g.strokePath (outPath, juce::PathStrokeType (1.4f));

        g.setColour (colors::gainRed);
        g.strokePath (grPath, juce::PathStrokeType (1.4f));

        if (scTraceVisible)
        {
            g.setColour (colors::sidechain);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText ("SC", (int) area.getX() + 4, (int) area.getBottom() - 16,
                        24, 12, juce::Justification::left);
        }
    }

    // Parameter overlays: the controls' footprint on the programme view —
    // threshold line with its knee band, and the Range cap in the GR band.
    void drawControlOverlays (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const float dashes[] = { 4.0f, 4.0f };

        if (kneeDb > 0.01f)
        {
            const float yTop = dbToY (juce::jmin (0.0f, thresholdDb + kneeDb * 0.5f), area);
            const float yBot = dbToY (thresholdDb - kneeDb * 0.5f, area);
            g.setColour (colors::accent.withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float> (area.getX() + 1.0f, yTop,
                                                area.getWidth() - 2.0f, yBot - yTop));
        }

        const float yThr = dbToY (thresholdDb, area);
        g.setColour (colors::accent.withAlpha (0.55f));
        g.drawDashedLine (juce::Line<float> (area.getX() + 2.0f, yThr,
                                             area.getRight() - 2.0f, yThr),
                          dashes, 2, 1.0f);
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText ("THR", (int) area.getRight() - 30, (int) yThr - 11, 26, 10,
                    juce::Justification::right);

        // Range only caps GR below its maximum; at full range there is
        // nothing to show.
        if (rangeDb < grScaleDb)
        {
            const float yRange = area.getY() + rangeDb / grScaleDb * area.getHeight() * 0.4f;
            g.setColour (colors::gainRed.withAlpha (0.5f));
            g.drawDashedLine (juce::Line<float> (area.getX() + 2.0f, yRange,
                                                 area.getRight() - 2.0f, yRange),
                              dashes, 2, 1.0f);
            g.drawText ("RANGE", (int) area.getRight() - 44, (int) yRange + 2, 40, 10,
                        juce::Justification::right);
        }
    }

    void drawKneePanel (juce::Graphics& g)
    {
        const auto panel = getLocalBounds().toFloat()
                               .removeFromTop (156.0f).removeFromRight (156.0f)
                               .reduced (8.0f);

        g.setColour (colors::background.withAlpha (0.85f));
        g.fillRoundedRectangle (panel, 3.0f);

        auto plot = panel.reduced (6.0f);

        auto toX = [plot] (float db) { return plot.getX() + (db - minDb) / -minDb * plot.getWidth(); };
        auto toY = [plot] (float db) { return plot.getBottom() - (db - minDb) / -minDb * plot.getHeight(); };

        // Unity reference.
        g.setColour (colors::outline);
        g.drawLine (toX (minDb), toY (minDb), toX (0.0f), toY (0.0f), 1.0f);

        // Threshold marker and knee span on the input axis.
        if (kneeDb > 0.01f)
        {
            const float xLo = toX (juce::jmax (minDb, thresholdDb - kneeDb * 0.5f));
            const float xHi = toX (juce::jmin (0.0f, thresholdDb + kneeDb * 0.5f));
            g.setColour (colors::accent.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float> (xLo, plot.getY(), xHi - xLo, plot.getHeight()));
        }
        const float xThr = toX (juce::jlimit (minDb, 0.0f, thresholdDb));
        g.setColour (colors::accent.withAlpha (0.35f));
        g.drawLine (xThr, plot.getY(), xThr, plot.getBottom(), 1.0f);

        // Transfer curve.
        juce::Path curve;
        for (int i = 0; i <= 60; ++i)
        {
            const float inDb = minDb + (float) i;
            const float outDb = juce::jlimit (minDb, 0.0f,
                                              curveEngine.computeOutputLevelDb (inDb));
            if (i == 0)
                curve.startNewSubPath (toX (inDb), toY (outDb));
            else
                curve.lineTo (toX (inDb), toY (outDb));
        }
        g.setColour (colors::accent);
        g.strokePath (curve, juce::PathStrokeType (1.6f));

        // Operating point: eased input level on the curve, pulled down by
        // the (eased) applied GR.
        const float inDb = juce::jlimit (minDb, 0.0f,
            juce::Decibels::gainToDecibels (easedIn, minDb));
        const float outDb = juce::jlimit (minDb, 0.0f, inDb - easedGr);
        g.setColour (colors::gainRed);
        g.fillEllipse (toX (inDb) - 3.5f, toY (outDb) - 3.5f, 7.0f, 7.0f);

        g.setColour (colors::outline);
        g.drawRoundedRectangle (panel, 3.0f, 1.0f);
    }

    void timerCallback() override
    {
        // Pull new scope frames.
        const auto writeIndex = processor.scopeWriteIndex.load (std::memory_order_acquire);
        auto behind = writeIndex - lastReadIndex;
        if (behind > (uint32_t) MeridianAudioProcessor::scopeSize)
        {
            lastReadIndex = writeIndex - (uint32_t) MeridianAudioProcessor::scopeSize;
            behind = (uint32_t) MeridianAudioProcessor::scopeSize;
        }

        for (uint32_t i = lastReadIndex; i != writeIndex; ++i)
        {
            const auto& f = processor.scopeRing[i & (uint32_t) (MeridianAudioProcessor::scopeSize - 1)];
            history.push_back (f);
            currentIn = f.inPk;
        }
        lastReadIndex = writeIndex;

        while (history.size() > 1024)
            history.pop_front();

        currentGr = processor.gainReductionDb.load();

        // Ease the operating point (QA pass): rise quickly, fall smoothly —
        // matching the meter ballistics so all motion reads as one system.
        easedIn += (currentIn > easedIn ? 0.6f : 0.25f) * (currentIn - easedIn);
        easedGr += (currentGr > easedGr ? 0.6f : 0.25f) * (currentGr - easedGr);

        // Keep the transfer curve in sync with the parameters.
        CompressorEngine::Params p;
        auto& s = processor.apvts;
        p.thresholdDb = s.getRawParameterValue (id::threshold)->load();
        p.ratio = s.getRawParameterValue (id::ratio)->load();
        p.kneeDb = s.getRawParameterValue (id::knee)->load();
        p.style = (Style) (int) s.getRawParameterValue (id::style)->load();
        curveEngine.setParams (p);

        thresholdDb = p.thresholdDb;
        kneeDb = p.kneeDb;
        rangeDb = s.getRawParameterValue (id::range)->load();
        scTraceVisible = ((int) s.getRawParameterValue (id::scSource)->load() == 1
                          && processor.sidechainActive.load())
                         || (int) s.getRawParameterValue (id::triggerMode)->load()
                                != (int) TriggerMode::audio;

        repaint();
    }

    MeridianAudioProcessor& processor;
    CompressorEngine curveEngine;
    std::deque<MeridianAudioProcessor::ScopeFrame> history;
    uint32_t lastReadIndex = 0;
    float currentIn = 0.0f, currentGr = 0.0f;
    float easedIn = 0.0f, easedGr = 0.0f;

    // Control-overlay state, refreshed each tick from the parameters.
    float thresholdDb = 0.0f, kneeDb = 0.0f, rangeDb = 60.0f;
    bool scTraceVisible = false;
};

} // namespace mrdn
