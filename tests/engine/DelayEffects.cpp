// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The AUX-RTN delay page heard through a real DualDelay node with the shipped data: BEAT places the
// echo, FEEDBK sets the repeat ratio, COLOR moves the spectrum, SPREAD turns each repeat, and a change
// of any of them while a tail rings adds no step of its own.
#include <gtest/gtest.h>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <juce_dsp/juce_dsp.h>
#include "DelayModulation.h"
#include "SpatialConfig.h"
#include "SpatialTestData.h"

namespace
{
using Channels = std::vector<std::vector<float>>;
constexpr int busChannels = 16;
constexpr double rate = 48000.0;
constexpr int block = 128;
constexpr int samplesPerTick = 480;   // the engine role's 100 Hz control tick
constexpr int acnW = 0, acnY = 1, acnX = 3;
constexpr double pi = 3.141592653589793;

size_t frames (double seconds) { return (size_t) std::lround (seconds * rate); }

std::unique_ptr<a3::IemNode> shippedDualDelay()
{
    auto node = a3::IemNode::create (a3::IemKind::dualDelay, busChannels, busChannels);
    juce::String error;
    const auto config = a3::loadSpatialConfig (a3test::shippedSpatialDirectory, error);
    if (node == nullptr || ! config || ! node->setParameters (config->fxParameters).wasOk())
        throw std::runtime_error ("no DualDelay with the shipped data: " + error.toStdString());
    return node;
}

// The engine's order: per block the loop's raw values, then the node; every 10 ms a control tick.
struct DelayRig
{
    std::unique_ptr<a3::IemNode> node = shippedDualDelay();
    a3::DelayLoop loop { *node };
    a3::DelayShape shape { *node };
    float tempo = 120.0f, beats = a3::delay::defaultBeat, feedback = a3::delay::defaultFeedback;
    float color = a3::delay::defaultColor, spread = a3::delay::defaultSpread;
    bool loopDriven = true;   // false: time and feedback stay as the test set them on the node
    int sinceTick = 0;

    DelayRig()
    {
        node->prepare (rate, block);
        loop.prepare (rate);
    }

    // `length` samples of output, all 16 channels; `in` (up to 16 channels, any length) is the input.
    Channels render (size_t length, const Channels& in = {})
    {
        Channels out ((size_t) busChannels, std::vector<float> (length));
        juce::AudioBuffer<float> io (busChannels, block);
        for (size_t start = 0; start < length; start += block)
        {
            const int n = (int) std::min ((size_t) block, length - start);
            io.clear();
            for (size_t ch = 0; ch < in.size(); ++ch)
                for (int i = 0; i < n && start + (size_t) i < in[ch].size(); ++i)
                    io.setSample ((int) ch, i, in[ch][start + (size_t) i]);
            if (loopDriven)
                loop.set (tempo, beats, feedback, n);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), busChannels, 0, n);
            node->process (view);
            for (int ch = 0; ch < busChannels; ++ch)
                std::copy_n (io.getReadPointer (ch), n, out[(size_t) ch].begin() + (long) start);
            for (sinceTick += n; sinceTick >= samplesPerTick; sinceTick -= samplesPerTick)
                shape.tick (color, spread, *node);
        }
        return out;
    }

    void settle (double seconds = 3.0) { render (frames (seconds)); }   // the 0.5 Hz time glide, the ramps
};

// A Hann-windowed tone or noise burst at the start of `length` samples.
std::vector<float> toneBurst (size_t length, double seconds, double hz, float level = 0.5f)
{
    std::vector<float> x (length, 0.0f);
    const size_t n = frames (seconds);
    for (size_t i = 0; i < n && i < length; ++i)
        x[i] = level * (float) (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) n)) * (float) std::sin (2.0 * pi * hz * (double) i / rate);
    return x;
}

std::vector<float> noiseBurst (size_t length, double seconds, float level = 0.5f)
{
    juce::Random random (1234);
    std::vector<float> x (length, 0.0f);
    for (size_t i = 0; i < frames (seconds) && i < length; ++i)
        x[i] = level * (2.0f * random.nextFloat() - 1.0f);
    return x;
}

// A plane wave from azimuth 0 at first order (SN3D): W = X = s.
Channels fromTheFront (const std::vector<float>& s)
{
    Channels in ((size_t) acnX + 1, std::vector<float> (s.size(), 0.0f));
    in[acnW] = in[acnX] = s;
    return in;
}

float peakIn (const std::vector<float>& x, size_t from, size_t to)
{
    float peak = 0.0f;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        peak = std::max (peak, std::abs (x[i]));
    return peak;
}

size_t peakAt (const std::vector<float>& x, size_t from, size_t to)
{
    size_t best = from;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        if (std::abs (x[i]) > std::abs (x[best]))
            best = i;
    return best;
}

// The largest sample-to-sample step over every channel in [from, to).
float largestStep (const Channels& out, size_t from, size_t to)
{
    float step = 0.0f;
    for (const auto& x : out)
        for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
            step = std::max (step, std::abs (x[i] - x[i - 1]));
    return step;
}

double echoSamples (float tempo, float beats) { return 60.0 * beats / tempo * rate; }

// Centroid in Hz of the magnitude spectrum of x[from, from + 4096).
double spectralCentroid (const std::vector<float>& x, size_t from)
{
    constexpr int order = 12, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (2 * size, 0.0f);
    std::copy_n (x.begin() + (long) from, size, data.begin());
    fft.performFrequencyOnlyForwardTransform (data.data());
    double weighted = 0.0, total = 0.0;
    for (int bin = 1; bin <= size / 2; ++bin)
    {
        weighted += (double) data[(size_t) bin] * bin * rate / size;
        total += data[(size_t) bin];
    }
    return weighted / total;
}

// Degrees, by the first-order intensity of [from, to): atan2 (sum W Y, sum W X).
double azimuthOf (const Channels& out, size_t from, size_t to)
{
    double y = 0.0, x = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        y += (double) out[acnW][i] * out[acnY][i];
        x += (double) out[acnW][i] * out[acnX][i];
    }
    return std::atan2 (y, x) * 180.0 / pi;
}

double angleBetween (double a, double b)
{
    const double d = std::fmod (std::abs (a - b), 360.0);
    return std::min (d, 360.0 - d);
}
}

TEST (DelayEffects, BeatPlacesTheFirstEchoAtItsTime)
{
    const std::vector<std::pair<float, float>> cases { { 120.0f, 0.125f }, { 120.0f, 0.25f }, { 120.0f, 0.5f },
                                                       { 120.0f, 0.75f }, { 120.0f, 1.0f }, { 120.0f, 2.0f },
                                                       { 120.0f, 4.0f }, { 200.0f, 4.0f } };
    for (const auto& [tempo, beats] : cases)
    {
        DelayRig rig;
        rig.tempo = tempo;
        rig.beats = beats;
        rig.settle();
        const float played = a3::delay::fitBeatsToLine (tempo, beats);   // R2's fallback where the line is too short
        const double expected = echoSamples (tempo, played);
        std::vector<float> impulse (frames (0.1) + (size_t) (1.6 * expected), 0.0f);
        impulse[0] = 1.0f;
        const auto out = rig.render (impulse.size(), { impulse });
        EXPECT_NEAR ((double) peakAt (out[acnW], 1, impulse.size()), expected, block) << tempo << " BPM, " << beats << " beats";
    }
}

TEST (DelayEffects, FeedbackIsTheRatioOfSuccessiveRepeats)
{
    for (const float p : { 0.0f, 0.25f, 0.5f, 0.95f })
    {
        DelayRig rig;
        rig.feedback = p;
        rig.settle();
        const auto d = (size_t) echoSamples (rig.tempo, rig.beats);   // 6000 samples
        const auto out = rig.render (4 * d + frames (0.05), { toneBurst (frames (0.02), 0.02, 1000.0) });
        const auto repeat = [&] (size_t k) { return peakIn (out[acnW], k * d - block, k * d + frames (0.02) + 2 * block); };
        const float first = repeat (1), second = repeat (2), third = repeat (3);
        ASSERT_GT (first, 0.1f) << p;
        if (p == 0.0f)
        {
            EXPECT_LT (second, 1e-4f * first) << "one repeat only";
            continue;
        }
        // The filters (233 Hz / 8 kHz) take 0.2 % of a 1 kHz tone per pass.
        EXPECT_NEAR (second / first, p, 0.02f * p + 0.005f) << p;
        EXPECT_NEAR (third / second, p, 0.02f * p + 0.005f) << p;
    }
}

TEST (DelayEffects, ColorLowersTheCentroidStrictly)
{
    double previous = 1e9;
    for (const float c : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        DelayRig rig;
        rig.color = c;
        rig.settle (1.0);
        const auto d = (size_t) echoSamples (rig.tempo, rig.beats);
        const auto out = rig.render (2 * d, { noiseBurst (frames (0.05), 0.05) });
        const double centroid = spectralCentroid (out[acnW], d - 64);
        std::cout << "COLOR " << c << ": centroid " << centroid << " Hz\n";
        EXPECT_LT (centroid, previous) << c;
        previous = centroid;
    }
}

// The right line is muted in the output and in the left line's cross feedback (test only): it turns
// the other way, and its -s would mix into the left line's repeats from the second on.
TEST (DelayEffects, SpreadTurnsEachRepeatBySDegrees)
{
    for (const float s : { 30.0f, 90.0f })
    {
        DelayRig rig;
        rig.loopDriven = false;   // the data's time (129.02 BPM x 4) and -6 dB feedback
        ASSERT_TRUE (rig.node->setParameter ("wetGainR", -60.0f).wasOk());
        ASSERT_TRUE (rig.node->setParameter ("xfeedbackR", -60.0f).wasOk());
        rig.spread = s;
        rig.settle (0.5);
        const auto d = (size_t) std::lround (echoSamples (129.02f, 0.25f));
        const auto out = rig.render (3 * d, fromTheFront (noiseBurst (frames (0.02), 0.02)));
        for (const size_t k : { 1u, 2u })
        {
            const double azimuth = azimuthOf (out, k * d - block, k * d + frames (0.02) + 4 * block);
            EXPECT_LT (angleBetween (azimuth, k * s), 3.0) << "spread " << s << ", repeat " << k << ": " << azimuth;
        }
    }
}

namespace
{
// A tail of a 60 ms 1 kHz burst from the front rings at 0.95 (a repeat every 125 ms); `change`
// happens in the middle of the fourth repeat, while the tone passes the loop. The largest
// sample-to-sample step over every channel from the change on, over the largest step of the
// first three repeats (the ringing's own). Not per repeat relative to its peak: the loop's filters
// raise a repeat's step / peak with every pass by themselves (0.13 -> 0.20 on the rotation-free W
// by the tenth, the same for a spread up or down).
double stepRatioOnChange (const std::function<void (DelayRig&)>& change, const char* name)
{
    DelayRig rig;
    rig.feedback = 0.95f;
    rig.settle();
    const auto d = (size_t) echoSamples (rig.tempo, rig.beats);
    const size_t burst = frames (0.06), changeAt = 4 * d + burst / 2;
    const auto ringing = rig.render (changeAt, fromTheFront (toneBurst (burst, 0.06, 1000.0)));
    const float before = largestStep (ringing, 0, 3 * d + burst + 4 * block);
    change (rig);
    const auto after = rig.render (10 * d);
    float step = largestStep (after, 0, after[0].size());
    for (size_t ch = 0; ch < after.size(); ++ch)   // and the step across the change
        step = std::max (step, std::abs (after[ch].front() - ringing[ch].back()));
    std::cout << name << ": largest step " << step << " from the change on, " << before << " while ringing before\n";
    return (double) step / (double) before;
}
}

TEST (DelayEffects, FeedbackColorAndSpreadChangeWithoutAStep)
{
    EXPECT_LE (stepRatioOnChange ([] (DelayRig& r) { r.feedback = 0.25f; }, "FEEDBK 0.95 -> 0.25"), 1.5);
    EXPECT_LE (stepRatioOnChange ([] (DelayRig& r) { r.feedback = 0.0f; }, "FEEDBK 0.95 -> 0"), 1.5);
    EXPECT_LE (stepRatioOnChange ([] (DelayRig& r) { r.color = 1.0f; }, "COLOR 0 -> +1"), 1.5);
    EXPECT_LE (stepRatioOnChange ([] (DelayRig& r) { r.color = -1.0f; }, "COLOR 0 -> -1"), 1.5);
    EXPECT_LE (stepRatioOnChange ([] (DelayRig& r) { r.spread = 180.0f; }, "SPREAD 60 -> 180"), 1.5);
    EXPECT_LE (stepRatioOnChange ([] (DelayRig& r) { r.spread = 0.0f; }, "SPREAD 60 -> 0"), 1.5);
}

namespace
{
struct GlideMeasure
{
    double lowestHz, highestHz;   // the tone's frequency through the glide (zero crossings)
    double settledAfter;          // seconds until it stays within 1 Hz of 1 kHz
    double stepRatio;             // largest step after the change / before it
};

// A steady 1 kHz tone through the delay with no feedback (one echo, so the output is the tone
// read at the gliding time); BEAT changes from `from` to `to` at 120 BPM.
GlideMeasure beatChange (float from, float to)
{
    DelayRig rig;
    rig.feedback = 0.0f;
    rig.beats = from;
    const size_t before = frames (3.0), after = frames (4.0);
    std::vector<float> tone (before + after);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.5f * (float) std::sin (2.0 * pi * 1000.0 * (double) i / rate);
    const auto first = rig.render (before, { std::vector<float> (tone.begin(), tone.begin() + (long) before) });
    rig.beats = to;
    const auto second = rig.render (after, { std::vector<float> (tone.begin() + (long) before, tone.end()) });

    const auto& w = second[acnW];
    GlideMeasure m { 1e9, 0.0, 0.0, 0.0 };
    double lastCrossing = -1.0, lastOff = 0.0;
    for (size_t i = 1; i < w.size(); ++i)
    {
        if (! (w[i - 1] < 0.0f && w[i] >= 0.0f))
            continue;
        const double crossing = (double) (i - 1) + (double) (-w[i - 1]) / (double) (w[i] - w[i - 1]);
        if (lastCrossing >= 0.0)
        {
            const double hz = rate / (crossing - lastCrossing);
            m.lowestHz = std::min (m.lowestHz, hz);
            m.highestHz = std::max (m.highestHz, hz);
            if (std::abs (hz - 1000.0) > 1.0)
                lastOff = crossing / rate;
        }
        lastCrossing = crossing;
    }
    m.settledAfter = lastOff;
    m.stepRatio = (double) largestStep ({ w }, 0, w.size()) / (double) largestStep ({ first[acnW] }, frames (2.0), before);
    return m;
}
}

// BEAT cannot be smoothed by the engine: DualDelay glides its time with a 0.5 Hz one-pole of its
// own (IEM compiled unchanged), so a change is a tape-like pitch swoop, not a click. Measured and
// printed for the report; asserted: the output stays finite, settles, and a small change has no step.
TEST (DelayEffects, ABeatChangeIsAPitchSwoopNotAClick)
{
    for (const auto& [from, to] : std::vector<std::pair<float, float>> { { 0.25f, 0.5f }, { 0.5f, 0.25f },
                                                                         { 0.25f, 0.125f }, { 0.125f, 2.0f },
                                                                         { 2.0f, 0.125f } })
    {
        const auto m = beatChange (from, to);
        std::cout << "BEAT " << from << " -> " << to << " at 120 BPM: tone 1000 Hz glides between " << m.lowestHz
                  << " and " << m.highestHz << " Hz, within 1 Hz after " << m.settledAfter << " s, largest step x"
                  << m.stepRatio << " of the steady tone's\n";
        EXPECT_TRUE (std::isfinite (m.stepRatio)) << from << " -> " << to;
        // Ten of IEM's time constants (1 / (2 pi f)) at most: 3.2 s at 0.5 Hz.
        EXPECT_LT (m.settledAfter, 10.0 / (2.0 * pi * a3::delay::iemDelayTimeGlideHz)) << from << " -> " << to;
    }
    EXPECT_LE (beatChange (0.25f, 0.5f).stepRatio, 1.5) << "a longer time slows the tone down: no step";
}
