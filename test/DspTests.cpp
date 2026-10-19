// Offline DSP verification for Meridian.
//
// Instantiates the processor directly, drives test signals through it, and
// checks measured gain reduction against the compressor's specified static
// curve and ballistics. Exits non-zero on failure.

#include "../src/PluginProcessor.h"
#include <cstdio>
#include <vector>

namespace
{

int failures = 0;

void expect (bool ok, const char* what, double got, double expected, double tol)
{
    std::printf ("%-62s got %9.3f  expected %9.3f (+/-%.3f)  %s\n",
                 what, got, expected, tol, ok ? "OK" : "FAIL");
    if (! ok)
        ++failures;
}

void expectNear (const char* what, double got, double expected, double tol)
{
    expect (std::abs (got - expected) <= tol, what, got, expected, tol);
}

void setParam (MeridianAudioProcessor& p, const juce::String& id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

constexpr double sr = 48000.0;
constexpr int blockSize = 512;

// Feeds a stereo sine and returns the steady-state output gain in dB
// (measured over the last 40 % of the run).
double measureSineGainDb (MeridianAudioProcessor& p, double freq, double amp,
                          int numBlocks = 300)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;

    double phase = 0.0;
    const double inc = juce::MathConstants<double>::twoPi * freq / sr;
    double sumSq = 0.0;
    long count = 0;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float v = (float) (amp * std::sin (phase));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
            phase += inc;
        }

        p.processBlock (buf, midi);

        if (b >= (numBlocks * 3) / 5)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double v = buf.getSample (0, i);
                sumSq += v * v;
                ++count;
            }
        }
    }

    const double rms = std::sqrt (sumSq / (double) count);
    return 20.0 * std::log10 (rms / (amp / std::sqrt (2.0)));
}

std::unique_ptr<MeridianAudioProcessor> makeProcessor (int numChannels = 2)
{
    auto p = std::make_unique<MeridianAudioProcessor>();
    p->setPlayConfigDetails (numChannels, numChannels, sr, blockSize);
    p->prepareToPlay (sr, blockSize);
    return p;
}

// Feeds a constant-level signal (every sample = amp) and returns the
// steady-state output level of channel 0 in dB. With the peak detector this
// makes the static curve exactly measurable.
double measureConstantOutDb (MeridianAudioProcessor& p, float amp,
                             int numChannels = 2, int numBlocks = 400)
{
    juce::AudioBuffer<float> buf (numChannels, blockSize);
    juce::MidiBuffer midi;
    float last = 0.0f;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::fill (buf.getWritePointer (ch), amp, blockSize);

        p.processBlock (buf, midi);
        last = buf.getSample (0, blockSize - 1);
    }

    return juce::Decibels::gainToDecibels ((double) std::abs (last), -200.0);
}

// Runs a step signal (silence, then constant amp) and returns the per-sample
// gain reduction trace in dB from the step onward.
std::vector<double> grTraceForStep (MeridianAudioProcessor& p, float amp,
                                    int silentBlocks, int loudBlocks)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    std::vector<double> trace;
    trace.reserve ((size_t) loudBlocks * blockSize);

    for (int b = 0; b < silentBlocks + loudBlocks; ++b)
    {
        const float v = b < silentBlocks ? 0.0f : amp;
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buf.getWritePointer (ch), v, blockSize);

        p.processBlock (buf, midi);

        if (b >= silentBlocks)
            for (int i = 0; i < blockSize; ++i)
                trace.push_back (-juce::Decibels::gainToDecibels (
                    (double) std::abs (buf.getSample (0, i)) / (double) amp, -200.0));
    }

    return trace;
}

// Index where the trace first crosses `level`, or -1.
int firstCrossing (const std::vector<double>& trace, double level, bool rising)
{
    for (size_t i = 0; i < trace.size(); ++i)
        if (rising ? trace[i] >= level : trace[i] <= level)
            return (int) i;
    return -1;
}

void testLatencyAndDefaults()
{
    auto p = makeProcessor();
    expectNear ("reported latency (samples)", p->getLatencySamples(), 0.0, 0.0);

    // Defaults: threshold 0 dB, ratio 2 — a -20 dB signal must pass untouched.
    juce::AudioBuffer<float> buf (2, blockSize), ref (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random rng (42);

    double maxDiff = 0.0;
    for (int b = 0; b < 40; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buf.setSample (ch, i, 0.1f * (rng.nextFloat() * 2.0f - 1.0f));

        ref.makeCopyOf (buf);
        p->processBlock (buf, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                maxDiff = juce::jmax (maxDiff,
                    (double) std::abs (buf.getSample (ch, i) - ref.getSample (ch, i)));
    }

    expect (maxDiff < 1.0e-6, "null test below threshold (max abs diff)",
            maxDiff, 0.0, 1.0e-6);
}

// Static curve: hard knee, exact expected output levels.
void testStaticCurve()
{
    struct Case { float thresh, ratio, inDb; };
    const Case cases[] = {
        { -20.0f,  4.0f,  -6.0f },
        { -30.0f,  2.0f,  -6.0f },
        { -20.0f, 20.0f,  -3.0f },
        { -40.0f,  8.0f, -20.0f },
        { -20.0f,  4.0f, -30.0f },  // below threshold: unity
    };

    for (const auto& c : cases)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, c.thresh);
        setParam (*p, mrdn::id::ratio, c.ratio);
        setParam (*p, mrdn::id::attack, 0.1f);
        setParam (*p, mrdn::id::release, 10.0f);

        const float amp = juce::Decibels::decibelsToGain (c.inDb);
        const double over = juce::jmax (0.0, (double) c.inDb - (double) c.thresh);
        const double expected = over > 0.0
            ? (double) c.thresh + over / (double) c.ratio
            : (double) c.inDb;

        char name[96];
        std::snprintf (name, sizeof (name),
                       "static curve T=%g R=%g:1 in=%g dB -> out dB",
                       c.thresh, c.ratio, c.inDb);
        expectNear (name, measureConstantOutDb (*p, amp), expected, 0.1);
    }
}

void testSoftKnee()
{
    // At input == threshold with knee width W, reduction = (1 - 1/R) * W / 8.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::knee, 12.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);

    const float amp = juce::Decibels::decibelsToGain (-20.0f);
    const double expected = -20.0 - (1.0 - 0.25) * 12.0 / 8.0;
    expectNear ("soft knee W=12 R=4 at threshold -> out dB",
                measureConstantOutDb (*p, amp), expected, 0.1);

    // Just below the knee (threshold - W/2): unity.
    auto p2 = makeProcessor();
    setParam (*p2, mrdn::id::threshold, -20.0f);
    setParam (*p2, mrdn::id::ratio, 4.0f);
    setParam (*p2, mrdn::id::knee, 12.0f);
    setParam (*p2, mrdn::id::attack, 0.1f);
    setParam (*p2, mrdn::id::release, 10.0f);

    const float amp2 = juce::Decibels::decibelsToGain (-27.0f);
    expectNear ("soft knee: below knee start -> unity out dB",
                measureConstantOutDb (*p2, amp2), -27.0, 0.1);
}

void testMakeupGain()
{
    auto p = makeProcessor();
    setParam (*p, mrdn::id::makeup, 6.0f);
    // Below threshold, so output = input + makeup.
    expectNear ("makeup +6 dB below threshold -> out dB",
                measureConstantOutDb (*p, juce::Decibels::decibelsToGain (-20.0f)),
                -14.0, 0.1);
}

void testBallistics()
{
    // Step to -6 dB, threshold -20, ratio 20: final GR = 14 * (1 - 1/20) = 13.3 dB.
    // GR envelope is a one-pole approach: 63.2 % of final after one attack tau.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 20.0f);
    setParam (*p, mrdn::id::attack, 50.0f);
    setParam (*p, mrdn::id::release, 200.0f);

    const float amp = juce::Decibels::decibelsToGain (-6.0f);
    auto trace = grTraceForStep (*p, amp, 4, 100);

    const double finalGr = trace.back();
    expectNear ("ballistics: final GR (dB)", finalGr, 13.3, 0.3);

    const int idx = firstCrossing (trace, finalGr * 0.632, true);
    expectNear ("ballistics: attack time to 63 % (ms)",
                idx * 1000.0 / sr, 50.0, 12.0);

    // Release: after the loud phase, drop to a quiet (below-threshold) level
    // and time the decay of GR to 36.8 %.
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    std::vector<double> rel;

    for (int b = 0; b < 100; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.01f, blockSize);

        p->processBlock (buf, midi);
        for (int i = 0; i < blockSize; ++i)
            rel.push_back (-juce::Decibels::gainToDecibels (
                (double) std::abs (buf.getSample (0, i)) / 0.01, -200.0));
    }

    const int relIdx = firstCrossing (rel, finalGr * 0.368, false);
    expectNear ("ballistics: release time to 36.8 % (ms)",
                relIdx * 1000.0 / sr, 200.0, 50.0);
}

void testBypassNull()
{
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -40.0f);
    setParam (*p, mrdn::id::ratio, 10.0f);
    setParam (*p, mrdn::id::makeup, 12.0f);
    setParam (*p, mrdn::id::bypass, 1.0f);

    juce::AudioBuffer<float> buf (2, blockSize), ref (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random rng (7);

    double maxDiff = 0.0;
    for (int b = 0; b < 40; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buf.setSample (ch, i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));

        ref.makeCopyOf (buf);
        p->processBlock (buf, midi);

        if (b >= 4) // skip the bypass crossfade
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    maxDiff = juce::jmax (maxDiff,
                        (double) std::abs (buf.getSample (ch, i) - ref.getSample (ch, i)));
    }

    expect (maxDiff < 1.0e-6, "bypass null (max abs diff)", maxDiff, 0.0, 1.0e-6);
}

void testStereoLink()
{
    // Loud left channel must pull the quiet right channel down equally.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    float lastR = 0.0f;

    for (int b = 0; b < 400; ++b)
    {
        juce::FloatVectorOperations::fill (buf.getWritePointer (0), 0.5f, blockSize);   // -6 dB
        juce::FloatVectorOperations::fill (buf.getWritePointer (1), 0.05f, blockSize);  // -26 dB
        p->processBlock (buf, midi);
        lastR = buf.getSample (1, blockSize - 1);
    }

    // Linked GR from the left channel: over = 14 dB, GR = 10.5 dB.
    expectNear ("stereo link: quiet right channel out dB",
                juce::Decibels::gainToDecibels ((double) std::abs (lastR), -200.0),
                -26.0 - 10.5, 0.15);
}

void testMonoPath()
{
    auto p = makeProcessor (1);
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);

    expectNear ("mono path: static curve out dB",
                measureConstantOutDb (*p, 0.5f, 1), -20.0 + 14.0 / 4.0, 0.1);
}

void testRmsDetector()
{
    // For a constant-level signal RMS == peak, so the static curve matches.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);
    setParam (*p, mrdn::id::detector, (float) (int) mrdn::DetectorMode::rms);

    expectNear ("RMS detector: constant signal static curve out dB",
                measureConstantOutDb (*p, 0.5f), -16.5, 0.15);
}

// v0.2: styles remap the user parameters onto effective knee/ratio values
// (see CompressorEngine). Constant signals make RMS == peak, so the static
// curves stay exactly measurable.
void testStyleCurves()
{
    struct Case { mrdn::Style style; float knee, inDb; double expected; const char* name; };
    const Case cases[] = {
        // Smooth: knee floor 6 dB. At threshold: GR = (1-1/R) * W/8 = 0.5625.
        { mrdn::Style::smooth, 0.0f, -20.0f, -20.5625, "smooth: knee floor 6 at threshold -> out dB" },
        // Smooth far above the knee: standard curve.
        { mrdn::Style::smooth, 0.0f, -6.0f, -16.5, "smooth: above knee -> out dB" },
        // Vari-Mu: ratio 4 * 1.25 = 5, knee floor 18. in -6: over 14 > W/2 -> T + over/5.
        { mrdn::Style::variMu, 0.0f, -6.0f, -17.2, "vari-mu: effective ratio 5 -> out dB" },
        // Opto: knee floor 12. At threshold: GR = 0.75 * 12/8 = 1.125.
        { mrdn::Style::opto, 0.0f, -20.0f, -21.125, "opto: knee floor 12 at threshold -> out dB" },
        // Punch: knee capped at 6 even when the user asks for 24.
        { mrdn::Style::punch, 24.0f, -20.0f, -20.5625, "punch: knee capped at 6 -> out dB" },
    };

    for (const auto& c : cases)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 4.0f);
        setParam (*p, mrdn::id::attack, 0.1f);
        setParam (*p, mrdn::id::release, 10.0f);
        setParam (*p, mrdn::id::knee, c.knee);
        setParam (*p, mrdn::id::style, (float) (int) c.style);

        expectNear (c.name,
                    measureConstantOutDb (*p, juce::Decibels::decibelsToGain (c.inDb), 2, 600),
                    c.expected, 0.1);
    }
}

void testUnlinkedStereo()
{
    // Link off: a loud left channel must leave a below-threshold right
    // channel untouched while the left is compressed on its own envelope.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);
    setParam (*p, mrdn::id::linkAmount, 0.0f);

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    float lastL = 0.0f, lastR = 0.0f;

    for (int b = 0; b < 400; ++b)
    {
        juce::FloatVectorOperations::fill (buf.getWritePointer (0), 0.5f, blockSize);   // -6 dB
        juce::FloatVectorOperations::fill (buf.getWritePointer (1), 0.05f, blockSize);  // -26 dB
        p->processBlock (buf, midi);
        lastL = buf.getSample (0, blockSize - 1);
        lastR = buf.getSample (1, blockSize - 1);
    }

    expectNear ("unlinked: left compressed independently -> out dB",
                juce::Decibels::gainToDecibels ((double) std::abs (lastL), -200.0), -16.5, 0.1);
    expectNear ("unlinked: quiet right untouched -> out dB",
                juce::Decibels::gainToDecibels ((double) std::abs (lastR), -200.0), -26.0, 0.1);
}

void testOptoTwoStageRelease()
{
    // Opto release must be fast while GR is deep and slow near zero: the
    // instantaneous time constant measured low in the decay should be several
    // times the one measured high in the decay.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 20.0f);
    setParam (*p, mrdn::id::attack, 1.0f);
    setParam (*p, mrdn::id::release, 200.0f);
    setParam (*p, mrdn::id::style, (float) (int) mrdn::Style::opto);

    // Drive into deep GR, then drop below threshold and trace the decay.
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;

    for (int b = 0; b < 600; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.5f, blockSize);
        p->processBlock (buf, midi);
    }

    std::vector<double> gr;
    for (int b = 0; b < 1500; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.01f, blockSize);
        p->processBlock (buf, midi);
        for (int i = 0; i < blockSize; ++i)
            gr.push_back (-juce::Decibels::gainToDecibels (
                (double) std::abs (buf.getSample (0, i)) / 0.01, -200.0));
    }

    const int t12 = firstCrossing (gr, 12.0, false), t10 = firstCrossing (gr, 10.0, false);
    const int t1  = firstCrossing (gr, 1.0, false),  t08 = firstCrossing (gr, 0.8, false);
    expect (t12 >= 0 && t10 > t12 && t1 > t10 && t08 > t1,
            "opto release: decay crossings found (12/10/1/0.8 dB)",
            (double) t08, 1.0, 1.0e9);

    const double tauHigh = (t10 - t12) / std::log (12.0 / 10.0) / sr * 1000.0;
    const double tauLow  = (t08 - t1)  / std::log (1.0 / 0.8)  / sr * 1000.0;
    expect (tauLow / tauHigh > 3.0, "opto release: tau(low GR) / tau(high GR)",
            tauLow / tauHigh, 3.0, 1.0e9);
}

// v0.3: every style's static curve must match the engine's own effective
// transfer function (computed on a reference engine instance), at a level
// above and below the -24 dB threshold. Constant signals keep RMS == peak;
// De-Ess's HF-weighted detector is accounted for via detectorDcGain().
void testAllStyleCurves()
{
    for (int s = 0; s < (int) mrdn::Style::numStyles; ++s)
    {
        mrdn::CompressorEngine reference;
        reference.prepare (sr, 2);
        mrdn::CompressorEngine::Params rp;
        rp.thresholdDb = -24.0f;
        rp.ratio = 4.0f;
        rp.style = (mrdn::Style) s;
        reference.setParams (rp);

        for (const float inDb : { -10.0f, -40.0f })
        {
            const float detDb = inDb + juce::Decibels::gainToDecibels (
                                            reference.detectorDcGain());
            const double expected = (double) inDb
                + (double) (reference.computeOutputLevelDb (detDb) - detDb);

            auto p = makeProcessor();
            setParam (*p, mrdn::id::threshold, -24.0f);
            setParam (*p, mrdn::id::ratio, 4.0f);
            setParam (*p, mrdn::id::attack, 0.1f);
            setParam (*p, mrdn::id::release, 10.0f);
            setParam (*p, mrdn::id::style, (float) s);

            char name[96];
            std::snprintf (name, sizeof (name), "style %-10s static curve in=%g dB -> out dB",
                           mrdn::styleChoices[s].toRawUTF8(), inDb);
            expectNear (name,
                        measureConstantOutDb (*p, juce::Decibels::decibelsToGain (inDb), 2, 600),
                        expected, 0.15);
        }
    }
}

void testWideKnee()
{
    // 72 dB knee range: at threshold with W=48, GR = (1-1/R) * W/8 = 4.5 dB.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -24.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::knee, 48.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);

    expectNear ("wide knee W=48 at threshold -> out dB",
                measureConstantOutDb (*p, juce::Decibels::decibelsToGain (-24.0f)),
                -28.5, 0.1);
}

void testSubMillisecondAttack()
{
    // 0.05 ms attack: GR must reach 90 % of its final value within 1 ms.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 20.0f);
    setParam (*p, mrdn::id::attack, 0.05f);
    setParam (*p, mrdn::id::release, 500.0f);

    auto trace = grTraceForStep (*p, 0.5f, 4, 40);
    const double finalGr = trace.back();
    const int idx = firstCrossing (trace, finalGr * 0.9, true);
    expect (idx >= 0 && idx <= (int) (sr / 1000.0),
            "sub-ms attack: samples to 90 % GR", idx, 0.0, sr / 1000.0);
}

void testOversampling()
{
    // Static curves must survive every oversampling factor, and latency must
    // be reported once oversampling filters are in the path.
    struct Case { mrdn::Style style; int osChoice; };
    const Case cases[] = {
        { mrdn::Style::versatile, 1 },  // 2x
        { mrdn::Style::versatile, 3 },  // 8x
        { mrdn::Style::versatile, 5 },  // 32x
        { mrdn::Style::variMu,    2 },  // 4x
        { mrdn::Style::limit,     5 },  // 32x
    };

    for (const auto& c : cases)
    {
        mrdn::CompressorEngine reference;
        reference.prepare (sr, 2);
        mrdn::CompressorEngine::Params rp;
        rp.thresholdDb = -24.0f;
        rp.ratio = 4.0f;
        rp.style = c.style;
        reference.setParams (rp);
        const double expected = (double) reference.computeOutputLevelDb (-10.0f);

        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -24.0f);
        setParam (*p, mrdn::id::ratio, 4.0f);
        setParam (*p, mrdn::id::attack, 0.1f);
        setParam (*p, mrdn::id::release, 10.0f);
        setParam (*p, mrdn::id::style, (float) (int) c.style);
        setParam (*p, mrdn::id::oversampling, (float) c.osChoice);

        char name[96];
        std::snprintf (name, sizeof (name), "oversampling %s style %s static -> out dB",
                       mrdn::oversamplingChoices[c.osChoice].toRawUTF8(),
                       mrdn::styleChoices[(int) c.style].toRawUTF8());
        expectNear (name,
                    measureConstantOutDb (*p, juce::Decibels::decibelsToGain (-10.0f), 2, 600),
                    expected, 0.3);

        expect (p->getLatencySamples() > 0, "  ... reports nonzero latency",
                p->getLatencySamples(), 1.0, 1.0e9);
    }

    // Below-threshold sine through 4x: passthrough within 0.1 dB.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::oversampling, 2.0f);
    expectNear ("oversampling 4x below-threshold sine gain dB",
                measureSineGainDb (*p, 1000.0, 0.05), 0.0, 0.1);

    auto pOff = makeProcessor();
    expectNear ("oversampling off: zero latency", pOff->getLatencySamples(), 0.0, 0.0);
}

void testAutoRelease()
{
    // Program-dependent release: a 30 ms burst must recover much faster than
    // a 4 s sustain (the slow envelope only charges on sustained material).
    auto recoveryMs = [] (int loudBlocks)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 20.0f);
        setParam (*p, mrdn::id::attack, 5.0f);
        setParam (*p, mrdn::id::release, 200.0f);
        setParam (*p, mrdn::id::autoRelease, 1.0f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;

        auto run = [&] (float amp, int blocks, std::vector<double>* trace)
        {
            for (int b = 0; b < blocks; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    juce::FloatVectorOperations::fill (buf.getWritePointer (ch), amp, blockSize);
                p->processBlock (buf, midi);
                if (trace != nullptr)
                    for (int i = 0; i < blockSize; ++i)
                        trace->push_back (-juce::Decibels::gainToDecibels (
                            (double) std::abs (buf.getSample (0, i)) / 0.01, -200.0));
            }
        };

        run (0.5f, loudBlocks, nullptr);
        std::vector<double> gr;
        run (0.01f, 800, &gr);
        return firstCrossing (gr, 1.33, false) * 1000.0 / sr; // 10 % of max GR
    };

    // 30 ms burst (3 x 512 @ 48k) vs 4.3 s sustain.
    const double burst = recoveryMs (3), sustain = recoveryMs (400);
    expect (burst > 0 && sustain > 2.0 * burst,
            "auto release: sustain recovery / burst recovery", sustain / burst, 2.0, 1.0e9);
}

void testAutoThreshold()
{
    // Level-independent: with Auto Threshold on, the same offset must give
    // the same GR at very different input levels.
    auto steadyOutDb = [] (float inDb)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -6.0f); // offset below loudness
        setParam (*p, mrdn::id::ratio, 4.0f);
        setParam (*p, mrdn::id::attack, 0.1f);
        setParam (*p, mrdn::id::release, 10.0f);
        setParam (*p, mrdn::id::autoThreshold, 1.0f);
        return measureConstantOutDb (*p, juce::Decibels::decibelsToGain (inDb), 2, 800);
    };

    // Tracked loudness -> level, so over = 6 dB and GR = 6 * (1 - 1/4) = 4.5.
    const double quiet = steadyOutDb (-30.0f), loud = steadyOutDb (-10.0f);
    expectNear ("auto threshold: -30 dB input -> out dB", quiet, -34.5, 0.3);
    expectNear ("auto threshold: -10 dB input -> out dB", loud, -14.5, 0.3);
    expectNear ("auto threshold: GR difference across 20 dB levels",
                (quiet - -30.0) - (loud - -10.0), 0.0, 0.2);
}

void testDeEssFrequencyDependence()
{
    // The De-Ess detector must hear 8 kHz much louder than 200 Hz.
    auto gainAt = [] (double freq)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -30.0f);
        setParam (*p, mrdn::id::ratio, 4.0f);
        setParam (*p, mrdn::id::attack, 1.0f);
        setParam (*p, mrdn::id::release, 50.0f);
        setParam (*p, mrdn::id::style, (float) (int) mrdn::Style::deEss);
        return measureSineGainDb (*p, freq, 0.3);
    };

    const double lowGr = -gainAt (200.0), highGr = -gainAt (8000.0);
    expect (highGr > lowGr + 4.0, "de-ess: GR at 8 kHz vs 200 Hz (dB extra)",
            highGr - lowGr, 4.0, 1.0e9);
}

void testMixControl()
{
    // Mix 0: bit-exact dry even while compressing hard.
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 4.0f);
        setParam (*p, mrdn::id::mix, 0.0f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        double maxDiff = 0.0;
        for (int b = 0; b < 40; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.5f, blockSize);
            p->processBlock (buf, midi);
            if (b >= 25) // past the block ramp and the per-sample gain smoothing
                maxDiff = juce::jmax (maxDiff,
                    (double) std::abs (buf.getSample (0, blockSize - 1) - 0.5f));
        }
        expect (maxDiff < 1.0e-6, "mix 0: dry null while compressing", maxDiff, 0.0, 1.0e-6);
    }

    // GR at -6 dB in, T=-20, R=4 (hard knee): 10.4845 dB.
    const double inDb = juce::Decibels::gainToDecibels (0.5);
    const double gr = (inDb + 20.0) * 0.75;

    // Mix 50 %: amplitude blend of dry and wet.
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 4.0f);
        setParam (*p, mrdn::id::attack, 0.1f);
        setParam (*p, mrdn::id::release, 10.0f);
        setParam (*p, mrdn::id::mix, 50.0f);

        const double expected = 20.0 * std::log10 (
            0.5 * (0.5 + 0.5 * std::pow (10.0, -gr / 20.0))) ;
        expectNear ("mix 50%: parallel blend -> out dB",
                    measureConstantOutDb (*p, 0.5f), expected, 0.1);
    }

    // Mix 200 %: over-compression — applied GR is doubled.
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 4.0f);
        setParam (*p, mrdn::id::attack, 0.1f);
        setParam (*p, mrdn::id::release, 10.0f);
        setParam (*p, mrdn::id::mix, 200.0f);

        expectNear ("mix 200%: over-compression -> out dB",
                    measureConstantOutDb (*p, 0.5f), inDb - 2.0 * gr, 0.1);
    }
}

void testRangeControl()
{
    // Raw GR would be ~32 dB; Range 6 must cap it exactly.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -40.0f);
    setParam (*p, mrdn::id::ratio, 20.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);
    setParam (*p, mrdn::id::range, 6.0f);

    expectNear ("range 6 dB caps applied GR -> out dB",
                measureConstantOutDb (*p, 0.5f),
                juce::Decibels::gainToDecibels (0.5) - 6.0, 0.1);
}

void testHoldControl()
{
    // Time until GR drops 1 dB after the signal falls, with and without hold.
    auto dropMs = [] (float holdMs)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 20.0f);
        setParam (*p, mrdn::id::attack, 1.0f);
        setParam (*p, mrdn::id::release, 100.0f);
        setParam (*p, mrdn::id::hold, holdMs);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < 100; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.5f, blockSize);
            p->processBlock (buf, midi);
        }

        std::vector<double> gr;
        for (int b = 0; b < 100; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.01f, blockSize);
            p->processBlock (buf, midi);
            for (int i = 0; i < blockSize; ++i)
                gr.push_back (-juce::Decibels::gainToDecibels (
                    (double) std::abs (buf.getSample (0, i)) / 0.01, -200.0));
        }

        return firstCrossing (gr, gr.front() - 1.0, false) * 1000.0 / sr;
    };

    const double without = dropMs (0.0f), with = dropMs (200.0f);
    expectNear ("hold 200 ms delays release onset (ms)", with - without, 200.0, 40.0);
}

void testLookahead()
{
    // A step's leading edge must emerge already compressed when lookahead is
    // active (the envelope leads the delayed audio).
    auto stepPeak = [] (float lookaheadMs, int* latencyOut)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 20.0f);
        setParam (*p, mrdn::id::attack, 5.0f);
        setParam (*p, mrdn::id::release, 200.0f);
        setParam (*p, mrdn::id::lookahead, lookaheadMs);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<float> out;
        for (int b = 0; b < 40; ++b)
        {
            const float v = b < 8 ? 0.0f : 0.5f;
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), v, blockSize);
            p->processBlock (buf, midi);
            for (int i = 0; i < blockSize; ++i)
                out.push_back (buf.getSample (0, i));
        }

        if (latencyOut != nullptr)
            *latencyOut = p->getLatencySamples();

        // Find where the step emerges, then the peak over the next 1 ms.
        size_t onset = 0;
        while (onset < out.size() && std::abs (out[onset]) < 0.02f)
            ++onset;
        float peak = 0.0f;
        for (size_t i = onset; i < juce::jmin (out.size(), onset + 48); ++i)
            peak = juce::jmax (peak, std::abs (out[i]));
        return peak;
    };

    int latency0 = 0, latency10 = 0;
    const float without = stepPeak (0.0f, &latency0), with = stepPeak (10.0f, &latency10);

    expect (with < without * 0.5f, "lookahead 10 ms: step edge pre-compressed",
            with, (double) without * 0.5, 1.0e9);
    expectNear ("lookahead 0: reported latency", latency0, 0.0, 0.0);
    expectNear ("lookahead 10 ms: reported latency (samples)", latency10, sr * 0.010, 2.0);
}

void testAutoGain()
{
    // GR is ~10.5 dB; Intelligent Auto Gain must bring the output back to
    // roughly the input level once its tracker settles.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);
    setParam (*p, mrdn::id::autoGain, 1.0f);

    expectNear ("intelligent auto gain restores level -> out dB",
                measureConstantOutDb (*p, 0.5f, 2, 1200),
                juce::Decibels::gainToDecibels (0.5), 0.5);
}

void testCharacterStage()
{
    // Warm at high drive: strong level-dependent gain (saturation), with the
    // compressor idle.
    auto sineGain = [] (mrdn::CharacterMode mode, float drive, double amp,
                        int posChoice = 1)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::ratio, 1.0f); // compressor idle
        setParam (*p, mrdn::id::character, (float) (int) mode);
        setParam (*p, mrdn::id::drive, drive);
        setParam (*p, mrdn::id::characterPos, (float) posChoice);
        return measureSineGainDb (*p, 1000.0, amp);
    };

    const double gLow = sineGain (mrdn::CharacterMode::warm, 18.0f, 0.02);
    const double gHigh = sineGain (mrdn::CharacterMode::warm, 18.0f, 0.8);
    expect (gLow - gHigh > 3.0, "warm drive 18: level-dependent gain (dB)",
            gLow - gHigh, 3.0, 1.0e9);
    expectNear ("warm drive 18: small-signal near transparent (dB)", gLow, 0.0, 0.5);

    // Drift: slow gain wobble — per-window RMS must vary far more than Clean.
    auto rmsSpreadDb = [] (mrdn::CharacterMode mode)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::ratio, 1.0f);
        setParam (*p, mrdn::id::character, (float) (int) mode);
        setParam (*p, mrdn::id::drive, 24.0f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const double inc = juce::MathConstants<double>::twoPi * 1000.0 / sr;

        std::vector<double> windows;
        double sumSq = 0.0;
        int count = 0;
        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = (float) (0.1 * std::sin (phase));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
                phase += inc;
            }
            p->processBlock (buf, midi);
            for (int i = 0; i < blockSize; ++i)
            {
                sumSq += (double) buf.getSample (0, i) * buf.getSample (0, i);
                ++count;
            }
            if (count >= 12000) // ~0.25 s windows
            {
                windows.push_back (10.0 * std::log10 (sumSq / count));
                sumSq = 0.0;
                count = 0;
            }
        }

        // Skip the first window (parameter ramps settling).
        double lo = 1.0e9, hi = -1.0e9;
        for (size_t i = 1; i < windows.size(); ++i)
        {
            lo = juce::jmin (lo, windows[i]);
            hi = juce::jmax (hi, windows[i]);
        }
        return hi - lo;
    };

    expect (rmsSpreadDb (mrdn::CharacterMode::drift) > 0.2,
            "drift: slow gain wobble present (dB spread)",
            rmsSpreadDb (mrdn::CharacterMode::drift), 0.2, 1.0e9);
    expect (rmsSpreadDb (mrdn::CharacterMode::clean) < 0.02,
            "clean: no gain wobble (dB spread)",
            rmsSpreadDb (mrdn::CharacterMode::clean), 0.0, 0.02);

    // Pre vs post routing must actually change the result.
    auto routed = [] (int posChoice)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 20.0f);
        setParam (*p, mrdn::id::attack, 1.0f);
        setParam (*p, mrdn::id::release, 50.0f);
        setParam (*p, mrdn::id::character, (float) (int) mrdn::CharacterMode::warm);
        setParam (*p, mrdn::id::drive, 18.0f);
        setParam (*p, mrdn::id::characterPos, (float) posChoice);
        return measureSineGainDb (*p, 1000.0, 0.5);
    };

    expect (std::abs (routed (0) - routed (1)) > 0.3,
            "character pre vs post routing differs (dB)",
            std::abs (routed (0) - routed (1)), 0.3, 1.0e9);
}

void testParameterInterpolation()
{
    // A 12 dB makeup jump mid-stream must ramp, not step: the largest
    // sample-to-sample delta stays close to the sine's own slope.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::ratio, 1.0f);

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;
    const double inc = juce::MathConstants<double>::twoPi * 100.0 / sr;

    double maxDelta = 0.0;
    float prev = 0.0f;
    for (int b = 0; b < 60; ++b)
    {
        if (b == 30)
            setParam (*p, mrdn::id::makeup, 12.0f);

        for (int i = 0; i < blockSize; ++i)
        {
            const float v = (float) (0.25 * std::sin (phase));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
            phase += inc;
        }
        p->processBlock (buf, midi);

        for (int i = 0; i < blockSize; ++i)
        {
            const float s = buf.getSample (0, i);
            if (b > 0 || i > 0)
                maxDelta = juce::jmax (maxDelta, (double) std::abs (s - prev));
            prev = s;
        }
    }

    expect (maxDelta < 0.05, "makeup jump is click-free (max sample delta)",
            maxDelta, 0.0, 0.05);
}

// v0.5 ----------------------------------------------------------------------

void testVariableLinkAmount()
{
    // 50 % link: the quiet right channel's detector is half-blended with the
    // loud left channel's level.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);
    setParam (*p, mrdn::id::linkAmount, 50.0f);

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    float lastR = 0.0f;

    for (int b = 0; b < 400; ++b)
    {
        juce::FloatVectorOperations::fill (buf.getWritePointer (0), 0.5f, blockSize);   // -6 dB
        juce::FloatVectorOperations::fill (buf.getWritePointer (1), 0.05f, blockSize);  // -26 dB
        p->processBlock (buf, midi);
        lastR = buf.getSample (1, blockSize - 1);
    }

    // Right detector: 0.5*max + 0.5*own = 0.275 -> -11.21 dB -> GR 6.59 dB.
    const double det = 0.5 * 0.5 + 0.5 * 0.05;
    const double gr = (juce::Decibels::gainToDecibels (det) + 20.0) * 0.75;
    expectNear ("link 50%: right channel out dB",
                juce::Decibels::gainToDecibels ((double) std::abs (lastR), -200.0),
                -26.0 - gr, 0.15);
}

void testMidSideModes()
{
    auto makeMs = [] (mrdn::LinkMode mode)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 4.0f);
        setParam (*p, mrdn::id::attack, 0.1f);
        setParam (*p, mrdn::id::release, 10.0f);
        setParam (*p, mrdn::id::linkMode, (float) (int) mode);
        return p;
    };

    auto run = [] (MeridianAudioProcessor& p, float ampL, float ampR, int blocks = 400)
    {
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            juce::FloatVectorOperations::fill (buf.getWritePointer (0), ampL, blockSize);
            juce::FloatVectorOperations::fill (buf.getWritePointer (1), ampR, blockSize);
            p.processBlock (buf, midi);
        }
        return std::make_pair (buf.getSample (0, blockSize - 1),
                               buf.getSample (1, blockSize - 1));
    };

    // Mid Only on mono material: M = 0.5 (-6 dB) -> GR 10.48; L must equal R
    // exactly (mono compatibility).
    {
        auto p = makeMs (mrdn::LinkMode::midOnly);
        auto [l, r] = run (*p, 0.5f, 0.5f);
        expectNear ("mid only, mono in: out dB",
                    juce::Decibels::gainToDecibels ((double) std::abs (l), -200.0), -16.5, 0.1);
        expect (std::abs (l - r) < 1.0e-6, "mid only, mono in: L == R (correlation)",
                std::abs (l - r), 0.0, 1.0e-6);
    }

    // Side Only on mono material: S == 0 -> untouched.
    {
        auto p = makeMs (mrdn::LinkMode::sideOnly);
        auto [l, r] = run (*p, 0.5f, 0.5f);
        expectNear ("side only, mono in: unity out dB",
                    juce::Decibels::gainToDecibels ((double) std::abs (l), -200.0), -6.02, 0.1);
        juce::ignoreUnused (r);
    }

    // Side Only on pure side material (L = -R): S = 0.5 -> compressed.
    {
        auto p = makeMs (mrdn::LinkMode::sideOnly);
        auto [l, r] = run (*p, 0.5f, -0.5f);
        expectNear ("side only, side in: out dB",
                    juce::Decibels::gainToDecibels ((double) std::abs (l), -200.0), -16.5, 0.1);
        expect (std::abs (l + r) < 1.0e-6, "side only, side in: L == -R",
                std::abs (l + r), 0.0, 1.0e-6);
    }

    // Mid to Side: L=0.5, R=0 -> M=S=0.25. Detect M (-12 dB, over 8 -> GR 6),
    // applied to S only.
    {
        auto p = makeMs (mrdn::LinkMode::midToSide);
        auto [l, r] = run (*p, 0.5f, 0.0f);
        const double g = std::pow (10.0, -6.0 / 20.0);
        expectNear ("mid to side: L out", l, (float) (0.25 + 0.25 * g), 0.01);
        expectNear ("mid to side: R out", r, (float) (0.25 - 0.25 * g), 0.01);
    }

    // M/S round trip with an idle compressor: bit-transparent within float
    // rounding.
    {
        auto p = makeMs (mrdn::LinkMode::midOnly);
        setParam (*p, mrdn::id::ratio, 1.0f);
        setParam (*p, mrdn::id::threshold, 0.0f);

        juce::AudioBuffer<float> buf (2, blockSize), ref (2, blockSize);
        juce::MidiBuffer midi;
        juce::Random rng (11);
        double maxDiff = 0.0;
        for (int b = 0; b < 30; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buf.setSample (ch, i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));
            ref.makeCopyOf (buf);
            p->processBlock (buf, midi);
            if (b >= 10)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        maxDiff = juce::jmax (maxDiff,
                            (double) std::abs (buf.getSample (ch, i) - ref.getSample (ch, i)));
        }
        expect (maxDiff < 1.0e-6, "M/S round trip null (idle compressor)",
                maxDiff, 0.0, 1.0e-6);
    }
}

void testSidechainEq()
{
    // A brickwall low-cut at 1 kHz on the sidechain must stop a 200 Hz tone
    // from triggering compression.
    auto gainAt200 = [] (bool scBandOn)
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::id::threshold, -30.0f);
        setParam (*p, mrdn::id::ratio, 4.0f);
        setParam (*p, mrdn::id::attack, 1.0f);
        setParam (*p, mrdn::id::release, 50.0f);
        if (scBandOn)
        {
            setParam (*p, mrdn::scBandParamId (0, "type"),
                      (float) (int) mrdn::ScFilterType::lowCutBrickwall);
            setParam (*p, mrdn::scBandParamId (0, "freq"), 1000.0f);
            setParam (*p, mrdn::scBandParamId (0, "on"), 1.0f);
        }
        return measureSineGainDb (*p, 200.0, 0.5);
    };

    const double without = gainAt200 (false), with = gainAt200 (true);
    expect (without < -8.0, "sc eq off: 200 Hz compressed (dB)", without, -8.0, 1.0e9);
    expectNear ("sc eq brickwall low cut: 200 Hz passes uncompressed (dB)", with, 0.0, 0.3);

    // Audition + bell +12 dB @ 1 kHz: the monitored detector shows the boost.
    {
        auto p = makeProcessor();
        setParam (*p, mrdn::scBandParamId (1, "type"), (float) (int) mrdn::ScFilterType::bell);
        setParam (*p, mrdn::scBandParamId (1, "freq"), 1000.0f);
        setParam (*p, mrdn::scBandParamId (1, "gain"), 12.0f);
        setParam (*p, mrdn::scBandParamId (1, "q"), 0.71f);
        setParam (*p, mrdn::scBandParamId (1, "on"), 1.0f);
        setParam (*p, mrdn::id::audition, 1.0f);

        expectNear ("audition: bell +12 dB @ 1 kHz heard on detector (dB)",
                    measureSineGainDb (*p, 1000.0, 0.1), 12.0, 0.5);
        expectNear ("audition: bell +12 far below (100 Hz) ~ unity (dB)",
                    measureSineGainDb (*p, 100.0, 0.1), 0.0, 0.75);
    }
}

void testExternalSidechain()
{
    // Quiet main input, loud external sidechain: the sidechain must drive GR.
    auto p = std::make_unique<MeridianAudioProcessor>();
    auto layout = p->getBusesLayout();
    layout.inputBuses.set (1, juce::AudioChannelSet::stereo());
    const bool layoutOk = p->setBusesLayout (layout);
    expect (layoutOk, "external sidechain: stereo SC bus accepted",
            layoutOk ? 1.0 : 0.0, 1.0, 0.0);

    p->prepareToPlay (sr, blockSize);

    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);
    setParam (*p, mrdn::id::scSource, 1.0f); // external

    juce::AudioBuffer<float> buf (4, blockSize); // main L/R + SC L/R
    juce::MidiBuffer midi;
    float last = 0.0f;

    for (int b = 0; b < 400; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.05f, blockSize);  // -26 dB
        for (int ch = 2; ch < 4; ++ch)
            juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.5f, blockSize);   // -6 dB
        p->processBlock (buf, midi);
        last = buf.getSample (0, blockSize - 1);
    }

    // GR from the -6 dB sidechain: 10.48 dB applied to the -26 dB main.
    expectNear ("external sidechain drives GR -> main out dB",
                juce::Decibels::gainToDecibels ((double) std::abs (last), -200.0),
                -26.0 - 10.48, 0.15);
}

// v0.6 ----------------------------------------------------------------------

void testMomentaryLoudness()
{
    // BS.1770: K-weighting is calibrated so a 997 Hz sine reads its mean
    // square directly. Stereo -18 dBFS in both channels -> -18.0 LUFS
    // (per-channel -21.01, +3.01 for the channel sum).
    auto measure = [] (int numChannels, float ampDb)
    {
        auto p = makeProcessor (numChannels);
        setParam (*p, mrdn::id::ratio, 1.0f); // compressor idle

        juce::AudioBuffer<float> buf (numChannels, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const double inc = juce::MathConstants<double>::twoPi * 997.0 / sr;
        const float amp = juce::Decibels::decibelsToGain (ampDb);

        for (int b = 0; b < 200; ++b) // ~2.1 s, well past the 400 ms window
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = amp * (float) std::sin (phase);
                for (int ch = 0; ch < numChannels; ++ch)
                    buf.setSample (ch, i, v);
                phase += inc;
            }
            p->processBlock (buf, midi);
        }

        return (double) p->loudness.momentaryLufs.load();
    };

    expectNear ("LUFS-M: stereo 997 Hz @ -18 dBFS", measure (2, -18.0f), -18.0, 0.5);
    expectNear ("LUFS-M: mono 997 Hz @ -18 dBFS", measure (1, -18.0f), -21.0, 0.5);
    expectNear ("LUFS-M: stereo 997 Hz @ -36 dBFS", measure (2, -36.0f), -36.0, 0.5);
}

void testScopeRing()
{
    // The display ring must advance and carry sensible in/out/GR frames.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 10.0f);

    const auto before = p->scopeWriteIndex.load();

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    for (int b = 0; b < 200; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.5f, blockSize);
        p->processBlock (buf, midi);
    }

    const auto after = p->scopeWriteIndex.load();
    const int expectedFrames = 200 * blockSize / MeridianAudioProcessor::scopeCellSamples;
    expectNear ("scope ring: frames written", (double) (after - before),
                (double) expectedFrames, 2.0);

    const auto& f = p->scopeRing[(after - 1) & (uint32_t) (MeridianAudioProcessor::scopeSize - 1)];
    expectNear ("scope ring: input peak", f.inPk, 0.5, 0.01);
    expectNear ("scope ring: GR (dB)", f.grDb, 10.48, 0.2);
    expectNear ("scope ring: output peak dB",
                juce::Decibels::gainToDecibels ((double) f.outPk), -16.5, 0.2);

    // Internal source, no SC EQ, unity trim: the detector trace is the input.
    expectNear ("scope ring: sidechain peak", f.scPk, 0.5, 0.01);
}

void testCharacterMsMonoSafety()
{
    // v0.5 carry-over QA: with an M/S link mode the character stage
    // saturates the M/S channels rather than L/R (it runs inside the
    // oversampled region). On mono material the side channel is silent, so
    // saturating mid-only must keep L == R exactly — i.e. the M/S-domain
    // colouration never breaks mono compatibility. Decision: intentional.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::linkMode, (float) mrdn::LinkMode::midOnly);
    setParam (*p, mrdn::id::character, (float) mrdn::CharacterMode::warm);
    setParam (*p, mrdn::id::drive, 12.0f);
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 4.0f);

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0, maxDiff = 0.0;
    const double inc = juce::MathConstants<double>::twoPi * 440.0 / sr;

    for (int b = 0; b < 100; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float v = 0.5f * (float) std::sin (phase);
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
            phase += inc;
        }
        p->processBlock (buf, midi);

        if (b > 20) // past the character stage's DC-blocker settling
            for (int i = 0; i < blockSize; ++i)
                maxDiff = juce::jmax (maxDiff,
                    (double) std::abs (buf.getSample (0, i) - buf.getSample (1, i)));
    }

    expectNear ("character in M/S mode: mono stays L == R", maxDiff, 0.0, 1.0e-6);
}

void testStateVersioning()
{
    // States written by a newer schema must be ignored, not half-applied
    // (the standalone app keeps stale state across dev builds).
    auto p = makeProcessor();
    setParam (*p, mrdn::id::threshold, -24.0f);

    auto xml = p->apvts.copyState().createXml();
    xml->setAttribute ("stateVersion", MeridianAudioProcessor::stateVersion + 1);
    setParam (*p, mrdn::id::threshold, -6.0f);

    const bool applied = p->applyStateXml (*xml);
    expect (! applied, "state versioning: newer schema rejected",
            applied ? 1.0 : 0.0, 0.0, 0.0);
    expectNear ("state versioning: params untouched by rejected state",
                p->apvts.getRawParameterValue (mrdn::id::threshold)->load(), -6.0, 1.0e-4);

    // Current and legacy (attribute-less) states still load.
    xml->setAttribute ("stateVersion", MeridianAudioProcessor::stateVersion);
    const bool appliedCurrent = p->applyStateXml (*xml);
    expect (appliedCurrent, "state versioning: current schema accepted",
            appliedCurrent ? 1.0 : 0.0, 1.0, 0.0);
    expectNear ("state versioning: accepted state applied",
                p->apvts.getRawParameterValue (mrdn::id::threshold)->load(), -24.0, 1.0e-4);

    xml->removeAttribute ("stateVersion");
    const bool appliedLegacy = p->applyStateXml (*xml);
    expect (appliedLegacy, "state versioning: legacy state accepted",
            appliedLegacy ? 1.0 : 0.0, 1.0, 0.0);
}

// v0.7 ----------------------------------------------------------------------

struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo pi;
        pi.setBpm (bpm);
        pi.setPpqPosition (ppq);
        pi.setIsPlaying (playing);
        return pi;
    }
};

void testHostSyncTrigger()
{
    // 120 bpm, 1/4-note sync -> a 24000-sample cycle at 48 kHz; pulse length
    // 50% ducks the first half of each cycle, offset 50% shifts the duck to
    // the second half. Measured in the third cycle so envelopes are settled.
    auto runSync = [] (float offsetPct, double& peakMidPulseWindow, double& peakEndWindow)
    {
        auto p = makeProcessor();
        FakePlayHead ph;
        p->setPlayHead (&ph);

        setParam (*p, mrdn::id::triggerMode, (float) mrdn::TriggerMode::hostSync);
        setParam (*p, mrdn::id::syncRate, 3.0f);      // 1/4
        setParam (*p, mrdn::id::pulseLength, 50.0f);
        setParam (*p, mrdn::id::syncOffset, offsetPct);
        setParam (*p, mrdn::id::style, (float) mrdn::Style::digital);
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 20.0f);
        setParam (*p, mrdn::id::attack, 0.1f);
        setParam (*p, mrdn::id::release, 20.0f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        const double ppqPerSample = 120.0 / (60.0 * sr);
        const long cycle = 24000, start = 2 * cycle;

        peakMidPulseWindow = peakEndWindow = 0.0;
        for (long total = 0; total < 3 * cycle; total += blockSize)
        {
            ph.ppq = (double) total * ppqPerSample;
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.5f, blockSize);
            p->processBlock (buf, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                const long g = total + i;
                if (g < start)
                    continue;
                const long inCycle = g - start;
                const double a = std::abs (buf.getSample (0, i));
                if (inCycle >= 11500 && inCycle < 12000)
                    peakMidPulseWindow = juce::jmax (peakMidPulseWindow, a);
                else if (inCycle >= 23500 && inCycle < 24000)
                    peakEndWindow = juce::jmax (peakEndWindow, a);
            }
        }
    };

    // Offset 0: duck in the first half (19 dB GR -> ~0.056), recovered by the
    // cycle end.
    double w1 = 0.0, w2 = 0.0;
    runSync (0.0f, w1, w2);
    expect (w1 < 0.12, "host sync: ducked at mid-pulse", w1, 0.056, 0.07);
    expect (w2 > 0.4, "host sync: recovered at cycle end", w2, 0.5, 0.1);

    // Offset 50%: the duck moves to the second half.
    runSync (50.0f, w1, w2);
    expect (w1 > 0.4, "host sync offset 50%: untouched at mid-cycle", w1, 0.5, 0.1);
    expect (w2 < 0.12, "host sync offset 50%: ducked at cycle end", w2, 0.056, 0.07);

    // Transport stopped: no triggering, signal passes at full level.
    {
        auto p = makeProcessor();
        FakePlayHead ph;
        ph.playing = false;
        p->setPlayHead (&ph);
        setParam (*p, mrdn::id::triggerMode, (float) mrdn::TriggerMode::hostSync);
        setParam (*p, mrdn::id::style, (float) mrdn::Style::digital);
        setParam (*p, mrdn::id::threshold, -20.0f);
        setParam (*p, mrdn::id::ratio, 20.0f);
        setParam (*p, mrdn::id::attack, 0.1f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        float last = 0.0f;
        for (int b = 0; b < 40; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.5f, blockSize);
            p->processBlock (buf, midi);
            last = buf.getSample (0, blockSize - 1);
        }
        expectNear ("host sync: silent trigger when stopped", std::abs (last), 0.5, 0.01);
    }
}

void testMidiTrigger()
{
    auto p = makeProcessor();
    setParam (*p, mrdn::id::triggerMode, (float) mrdn::TriggerMode::midi);
    setParam (*p, mrdn::id::style, (float) mrdn::Style::digital);
    setParam (*p, mrdn::id::threshold, -20.0f);
    setParam (*p, mrdn::id::ratio, 20.0f);
    setParam (*p, mrdn::id::attack, 0.1f);
    setParam (*p, mrdn::id::release, 20.0f);

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    auto fill = [&buf] { for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.5f, blockSize); };

    // No notes -> untouched.
    for (int b = 0; b < 20; ++b) { fill(); p->processBlock (buf, midi); }
    expectNear ("midi trigger: untouched before note", std::abs (buf.getSample (0, blockSize - 1)),
                0.5, 0.01);

    // Note-on at sample 100 of the block: sample-accurate duck onset.
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 100);
    fill();
    p->processBlock (buf, midi);
    midi.clear();
    expect (std::abs (buf.getSample (0, 50)) > 0.4,
            "midi trigger: untouched before the note-on sample",
            std::abs (buf.getSample (0, 50)), 0.5, 0.1);
    expect (std::abs (buf.getSample (0, 400)) < 0.12,
            "midi trigger: ducked shortly after note-on",
            std::abs (buf.getSample (0, 400)), 0.056, 0.07);

    // Held -> stays ducked.
    for (int b = 0; b < 10; ++b) { fill(); p->processBlock (buf, midi); }
    expect (std::abs (buf.getSample (0, blockSize - 1)) < 0.12,
            "midi trigger: held note keeps ducking",
            std::abs (buf.getSample (0, blockSize - 1)), 0.056, 0.07);

    // Note-off -> releases back to unity.
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    fill();
    p->processBlock (buf, midi);
    midi.clear();
    for (int b = 0; b < 30; ++b) { fill(); p->processBlock (buf, midi); }
    expectNear ("midi trigger: recovered after note-off",
                std::abs (buf.getSample (0, blockSize - 1)), 0.5, 0.05);
}

void testMidiLearn()
{
    auto p = makeProcessor();
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;

    // Arm, then send CC 20 at 32: the mapping is captured and applied at
    // once (mix = 32/127 of 0..200%).
    p->armMidiLearn (mrdn::id::mix);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 32), 0);
    p->processBlock (buf, midi);
    midi.clear();
    expectNear ("midi learn: captured CC applies immediately",
                p->apvts.getRawParameterValue (mrdn::id::mix)->load(),
                32.0 / 127.0 * 200.0, 0.5);

    p->commitPendingMidiLearn();
    expectNear ("midi learn: mapping persisted in state",
                p->getMappedCc (mrdn::id::mix), 20.0, 0.0);

    // The mapping survives a state round-trip into a fresh processor.
    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto p2 = makeProcessor();
    p2->setStateInformation (state.getData(), (int) state.getSize());
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 127), 0);
    p2->processBlock (buf, midi);
    midi.clear();
    expectNear ("midi learn: mapping works after state reload",
                p2->apvts.getRawParameterValue (mrdn::id::mix)->load(), 200.0, 0.5);

    // Clearing the mapping stops CC control.
    p2->clearMidiMapping (mrdn::id::mix);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 0), 0);
    p2->processBlock (buf, midi);
    midi.clear();
    expectNear ("midi learn: cleared mapping is inert",
                p2->apvts.getRawParameterValue (mrdn::id::mix)->load(), 200.0, 0.5);
}

void testAutomationSmoothness()
{
    // Dense multi-parameter automation (new random targets every block for
    // threshold, makeup, mix, drive, and range) must never click: the
    // largest sample-to-sample output delta stays close to the sine's own
    // slope, as every audible parameter ramps over ~20 ms.
    auto p = makeProcessor();
    setParam (*p, mrdn::id::ratio, 4.0f);
    setParam (*p, mrdn::id::attack, 5.0f);
    setParam (*p, mrdn::id::release, 80.0f);

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random rng (7);
    double phase = 0.0;
    const double inc = juce::MathConstants<double>::twoPi * 100.0 / sr;

    double maxDelta = 0.0;
    float prev = 0.0f;
    for (int b = 0; b < 200; ++b)
    {
        setParam (*p, mrdn::id::threshold, -40.0f + 35.0f * rng.nextFloat());
        setParam (*p, mrdn::id::makeup, -6.0f + 12.0f * rng.nextFloat());
        setParam (*p, mrdn::id::mix, 50.0f + 100.0f * rng.nextFloat());
        setParam (*p, mrdn::id::drive, 6.0f * rng.nextFloat());
        setParam (*p, mrdn::id::range, 20.0f + 40.0f * rng.nextFloat());

        for (int i = 0; i < blockSize; ++i)
        {
            const float v = (float) (0.25 * std::sin (phase));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
            phase += inc;
        }
        p->processBlock (buf, midi);

        for (int i = 0; i < blockSize; ++i)
        {
            const float s = buf.getSample (0, i);
            if (b > 0 || i > 0)
                maxDelta = juce::jmax (maxDelta, (double) std::abs (s - prev));
            prev = s;
        }
    }

    expect (maxDelta < 0.05, "dense automation is click-free (max sample delta)",
            maxDelta, 0.0, 0.05);
}

// v0.8 ----------------------------------------------------------------------

float paramValue (MeridianAudioProcessor& p, const juce::String& id)
{
    return p.apvts.getRawParameterValue (id)->load();
}

void testPresetSystem()
{
    auto p = makeProcessor();
    auto& pm = p->presets;

    // Factory content is present.
    int factoryCount = 0;
    for (const auto& info : pm.getPresets())
        if (info.factory)
            ++factoryCount;
    expect (factoryCount >= 14, "presets: factory set present", factoryCount, 14.0, 99.0);

    // Loading a factory preset applies its declared values (overrides and
    // untouched defaults alike) and sets the preset name.
    const mrdn::PresetInfo* glue = nullptr;
    for (const auto& info : pm.getPresets())
        if (info.name == "Mix Bus Glue")
            glue = &info;

    expect (glue != nullptr, "presets: factory preset found by name",
            glue != nullptr ? 1.0 : 0.0, 1.0, 0.0);
    if (glue != nullptr)
    {
        setParam (*p, mrdn::id::threshold, -50.0f); // will be overwritten
        const bool loaded = pm.loadPreset (*glue);
        expect (loaded, "presets: factory preset loads", loaded ? 1.0 : 0.0, 1.0, 0.0);
        expectNear ("presets: style applied",
                    paramValue (*p, mrdn::id::style), (double) (int) mrdn::Style::bus, 1.0e-4);
        expectNear ("presets: ratio applied", paramValue (*p, mrdn::id::ratio), 2.0, 1.0e-3);
        expectNear ("presets: non-override reset to default",
                    paramValue (*p, mrdn::id::mix), 100.0, 1.0e-3);
        expect (p->stateManager.getCurrentPresetName() == "Mix Bus Glue",
                "presets: current preset name tracked",
                p->stateManager.getCurrentPresetName() == "Mix Bus Glue" ? 1.0 : 0.0, 1.0, 0.0);
    }

    // Metadata wrapper round-trip (in memory; no files touched).
    const auto presetCfg = mrdn::makePresetConfig();
    juce::XmlElement wrapper (presetCfg.presetTag.toString());
    wrapper.setAttribute ("stateVersion", presetCfg.stateVersion);
    wrapper.setAttribute ("name", "Test Sound");
    wrapper.setAttribute ("tags", "alpha;beta");
    wrapper.addChildElement (p->apvts.copyState().createXml().release());

    auto unwrapped = mrdn::PresetManager::stateFromPresetXml (wrapper, presetCfg);
    expect (unwrapped.isValid(), "presets: metadata wrapper parses",
            unwrapped.isValid() ? 1.0 : 0.0, 1.0, 0.0);

    // Legacy bare-state presets still parse; newer-schema presets do not.
    auto legacyXml = p->apvts.copyState().createXml();
    auto legacyState = mrdn::PresetManager::stateFromPresetXml (*legacyXml, presetCfg);
    expect (legacyState.isValid(), "presets: legacy bare-state preset parses",
            legacyState.isValid() ? 1.0 : 0.0, 1.0, 0.0);

    wrapper.setAttribute ("stateVersion", presetCfg.stateVersion + 1);
    auto tooNew = mrdn::PresetManager::stateFromPresetXml (wrapper, presetCfg);
    expect (! tooNew.isValid(), "presets: newer-schema preset rejected",
            tooNew.isValid() ? 1.0 : 0.0, 0.0, 0.0);

    // A factory preset load keeps an existing MIDI mapping (presets without
    // a MIDIMAP inherit the session's).
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    p->armMidiLearn (mrdn::id::mix);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 21, 64), 0);
    p->processBlock (buf, midi);
    midi.clear();
    p->commitPendingMidiLearn();
    pm.loadPreset (pm.getPresets().front()); // "Init"
    expectNear ("presets: MIDI mapping survives factory preset load",
                p->getMappedCc (mrdn::id::mix), 21.0, 0.0);

    // Prev/next stepping wraps through the list in browser order.
    pm.loadAdjacent (true);
    const bool moved = p->stateManager.getCurrentPresetName() != "Init";
    expect (moved, "presets: next-preset steps away from Init", moved ? 1.0 : 0.0, 1.0, 0.0);
    pm.loadAdjacent (false);
    expect (p->stateManager.getCurrentPresetName() == "Init",
            "presets: prev-preset steps back to Init",
            p->stateManager.getCurrentPresetName() == "Init" ? 1.0 : 0.0, 1.0, 0.0);
}

void testAbComparison()
{
    auto p = makeProcessor();
    auto& sm = p->stateManager;

    // Undo/redo of an edit burst in slot A.
    setParam (*p, mrdn::id::threshold, -30.0f);
    sm.commitPendingChanges();
    expect (sm.canUndo(), "A/B: edit is undoable", sm.canUndo() ? 1.0 : 0.0, 1.0, 0.0);

    sm.undo();
    expectNear ("A/B: undo restores default threshold",
                paramValue (*p, mrdn::id::threshold), 0.0, 1.0e-4);
    sm.redo();
    expectNear ("A/B: redo reapplies edit",
                paramValue (*p, mrdn::id::threshold), -30.0, 1.0e-4);

    // Slot B still holds the construction-time state.
    sm.setActiveSlot (1);
    expectNear ("A/B: slot B untouched by slot A edits",
                paramValue (*p, mrdn::id::threshold), 0.0, 1.0e-4);

    // Edits in B don't leak into A...
    setParam (*p, mrdn::id::ratio, 8.0f);
    sm.commitPendingChanges();
    sm.setActiveSlot (0);
    expectNear ("A/B: back on A, its threshold is intact",
                paramValue (*p, mrdn::id::threshold), -30.0, 1.0e-4);
    expectNear ("A/B: B's ratio edit didn't leak into A",
                paramValue (*p, mrdn::id::ratio), 2.0, 1.0e-3);

    // ...and undo history is per slot: undoing in A must not undo B's edit.
    sm.undo();
    expectNear ("A/B: undo in A affects A",
                paramValue (*p, mrdn::id::threshold), 0.0, 1.0e-4);
    sm.setActiveSlot (1);
    expectNear ("A/B: B kept its own edit through A's undo",
                paramValue (*p, mrdn::id::ratio), 8.0, 1.0e-3);
    sm.undo();
    expectNear ("A/B: B's own undo restores its default",
                paramValue (*p, mrdn::id::ratio), 2.0, 1.0e-3);

    // Copy active -> other, undoable in the destination's history.
    sm.setActiveSlot (0);
    setParam (*p, mrdn::id::makeup, 6.0f);
    sm.commitPendingChanges();
    sm.copyActiveToOther();
    sm.setActiveSlot (1);
    expectNear ("A/B: copy carried the state across",
                paramValue (*p, mrdn::id::makeup), 6.0, 1.0e-3);
    sm.undo();
    expectNear ("A/B: copy is undoable in the destination slot",
                paramValue (*p, mrdn::id::makeup), 0.0, 1.0e-3);
}

void testAbSessionRoundTrip()
{
    auto p = makeProcessor();
    auto& sm = p->stateManager;

    setParam (*p, mrdn::id::threshold, -25.0f);   // slot A
    sm.commitPendingChanges();
    sm.setActiveSlot (1);
    setParam (*p, mrdn::id::ratio, 10.0f);        // slot B, left active
    sm.commitPendingChanges();

    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto p2 = makeProcessor();
    p2->setStateInformation (state.getData(), (int) state.getSize());

    expect (p2->stateManager.getActiveSlot() == 1,
            "A/B session: active slot restored",
            p2->stateManager.getActiveSlot(), 1.0, 0.0);
    expectNear ("A/B session: active slot values restored",
                paramValue (*p2, mrdn::id::ratio), 10.0, 1.0e-3);

    p2->stateManager.setActiveSlot (0);
    expectNear ("A/B session: inactive slot restored too",
                paramValue (*p2, mrdn::id::threshold), -25.0, 1.0e-4);
    expectNear ("A/B session: slot A didn't inherit B's ratio",
                paramValue (*p2, mrdn::id::ratio), 2.0, 1.0e-3);
}

} // namespace

int main()
{
    std::printf ("Meridian DSP tests (sr=%.0f, block=%d)\n\n", sr, blockSize);

    testLatencyAndDefaults();
    testStaticCurve();
    testSoftKnee();
    testMakeupGain();
    testBallistics();
    testBypassNull();
    testStereoLink();
    testMonoPath();
    testRmsDetector();
    testStyleCurves();
    testUnlinkedStereo();
    testOptoTwoStageRelease();
    testAllStyleCurves();
    testWideKnee();
    testSubMillisecondAttack();
    testOversampling();
    testAutoRelease();
    testAutoThreshold();
    testDeEssFrequencyDependence();
    testMixControl();
    testRangeControl();
    testHoldControl();
    testLookahead();
    testAutoGain();
    testCharacterStage();
    testParameterInterpolation();
    testVariableLinkAmount();
    testMidSideModes();
    testSidechainEq();
    testExternalSidechain();
    testMomentaryLoudness();
    testScopeRing();
    testCharacterMsMonoSafety();
    testStateVersioning();
    testHostSyncTrigger();
    testMidiTrigger();
    testMidiLearn();
    testAutomationSmoothness();
    testPresetSystem();
    testAbComparison();
    testAbSessionRoundTrip();

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
