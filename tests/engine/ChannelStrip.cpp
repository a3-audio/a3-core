// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include <memory>
#include "TestSignals.h"
#include "ChannelStrip.h"
#include "Laws.h"
#include "PurestGain.h"
#include "SmoothEQ3.h"

namespace
{
struct StripRun { a3test::Stereo moving, steady; };
StripRun run (a3::ChannelStrip& strip, const a3test::Stereo& in, const std::vector<int>& blockSizes)
{
    StripRun r { in, in };
    size_t s = 0; size_t k = 0;
    while (s < in.l.size())
    {
        const int n = (int) std::min<size_t> ((size_t) blockSizes[k++ % blockSizes.size()], in.l.size() - s);
        const float* i[2] { in.l.data() + s, in.r.data() + s };
        float* m[2] { r.moving.l.data() + s, r.moving.r.data() + s };
        float* st[2] { r.steady.l.data() + s, r.steady.r.data() + s };
        strip.process (i, m, st, n);
        s += (size_t) n;
    }
    return r;
}

// Built in place: StripControls holds atomics, so a strip cannot be returned by value.
std::unique_ptr<a3::ChannelStrip> made (float threeD, float freq, float q)
{
    auto strip = std::make_unique<a3::ChannelStrip>();
    strip->prepare (44100.0, 512);
    auto& c = strip->controls();
    c.gain = 1.0f; c.volume = 1.0f;                 // both 0 dB (A 0.5)
    c.eqHigh = c.eqMid = c.eqLow = 1.0f;            // slopeEq(1) = 0.6 — the desk's top, +0.13 dB max
    c.threeD = threeD; c.frequency = freq; c.q = q;
    return strip;
}

a3test::Stereo sum (const StripRun& r)
{
    a3test::Stereo s = r.moving;
    for (size_t i = 0; i < s.l.size(); ++i) { s.l[i] += r.steady.l[i]; s.r[i] += r.steady.r[i]; }
    return s;
}
}

// moving + steady = the EQ'd input, at every 3D/freq/Q.
TEST (ChannelStrip, MovingPlusSteadyIsTheInputAtEveryPosition)
{
    const auto in = a3test::noise (22050, 0.25f, 9);
    for (float threeD : { 0.0f, 0.3f, 1.0f })
        for (float f : { 0.0f, 0.5f, 1.0f })
            for (float q : { 0.0f, 1.0f })
            {
                auto strip = made (threeD, f, q);
                auto reference = made (0.0f, f, q);           // 3D 0: nothing moves, steady = the input
                const auto r = run (*strip, in, { 128 });
                const auto ref = run (*reference, in, { 128 });
                EXPECT_LE (a3test::maxAbsDiff (sum (r), ref.steady), 1e-5) << threeD << " " << f << " " << q;
            }
}

// The template's bed (n-multi-enc) takes the input straight, with no delay for
// Isolator3's frame: moving + steady is TRIM + EQ with nothing in between, and
// at 3D 0 the moving pair is exact silence.
TEST (ChannelStrip, TheBedIsTheUndelayedInputAtThreeDZero)
{
    const auto in = a3test::noise (22050, 0.25f, 3);
    auto strip = made (0.0f, 0.5f, 0.05f);
    const auto r = run (*strip, in, { 128 });

    a3::PurestGain trim;
    a3::SmoothEQ3 eq;
    trim.prepare (44100.0);
    eq.prepare (44100.0);
    trim.setParameters ((float) a3::laws::slopeVolume (1.0), 1.0f);
    const float top = (float) a3::laws::slopeEq (1.0);
    eq.setParameters (top, top, top);
    auto expected = a3test::runInBlocks (trim, in, 128);
    expected = a3test::runInBlocks (eq, expected, 128);

    EXPECT_LE (a3test::maxAbsDiff (r.steady, expected), 1e-6);
    EXPECT_LE (a3test::maxAbsDiff (sum (r), expected), 1e-6);
    for (size_t i = 0; i < r.moving.l.size(); ++i)
        ASSERT_EQ (r.moving.l[i], 0.0f) << i;
}

// Review focus 1: block sizes that change between calls and exceed maxBlock.
TEST (ChannelStrip, OddAndChangingBlockSizesNeitherCrashNorDiffer)
{
    const auto in = a3test::noise (44100, 0.25f, 4);
    auto a = made (0.6f, 0.4f, 0.2f), b = made (0.6f, 0.4f, 0.2f);
    const auto fixed = run (*a, in, { 128 });
    // Same boundaries every 128 frames, split oddly inside: 1+7+120, 64+64, 128 — then 4096 (> maxBlock 512).
    const auto odd = run (*b, in, { 1, 7, 120, 64, 64, 128 });
    EXPECT_LE (a3test::maxAbsDiff (fixed.moving, odd.moving), 1e-5);
    EXPECT_LE (a3test::maxAbsDiff (fixed.steady, odd.steady), 1e-5);
    auto c = made (0.6f, 0.4f, 0.2f);
    StripRun big;
    EXPECT_NO_FATAL_FAILURE (big = run (*c, in, { 4096 }));
    EXPECT_LE (a3test::maxAbsDiff (fixed.moving, big.moving), 1e-5);
    EXPECT_LE (a3test::maxAbsDiff (fixed.steady, big.steady), 1e-5);
}

// A control changed between calls takes effect at the next 128-frame grid
// boundary, whatever the call sizes.
TEST (ChannelStrip, ControlsTakeEffectOnTheGrid)
{
    const auto in = a3test::noise (128 * 40, 0.25f, 6);
    // 3D from 0 (band closed, moving exact silence) to 0.9: the strip's own
    // open/closed switch must follow the grid as the units' parameters do.
    auto a = made (0.0f, 0.4f, 0.2f), b = made (0.0f, 0.4f, 0.2f);
    auto runChanging = [&] (a3::ChannelStrip& strip, const std::vector<int>& sizes) {
        StripRun r { in, in };
        size_t s = 0, k = 0;
        while (s < in.l.size())
        {
            const int n = (int) std::min<size_t> ((size_t) sizes[k++ % sizes.size()], in.l.size() - s);
            // Set mid-block in the odd run, at the boundary in the whole run: both latch at 128*20.
            if (s > 128 * 19 && s <= 128 * 20)
            {
                strip.controls().threeD = 0.9f;
                strip.controls().volume = 0.7f;
            }
            const float* i[2] { in.l.data() + s, in.r.data() + s };
            float* m[2] { r.moving.l.data() + s, r.moving.r.data() + s };
            float* st[2] { r.steady.l.data() + s, r.steady.r.data() + s };
            strip.process (i, m, st, n);
            s += (size_t) n;
        }
        return r;
    };
    const auto whole = runChanging (*a, { 128 });
    const auto odd = runChanging (*b, { 1, 7, 120, 64, 64, 128 });
    EXPECT_EQ (a3test::maxAbsDiff (whole.moving, odd.moving), 0.0);
    EXPECT_EQ (a3test::maxAbsDiff (whole.steady, odd.steady), 0.0);
}

// Review focus 4: silence after signal is exact zero (the originals put out -145 dBFS
// noise; true zero is the deliberate difference).
TEST (ChannelStrip, SilenceStaysSilent)
{
    auto strip = made (0.7f, 0.5f, 0.3f);
    run (*strip, a3test::noise (4410, 0.5f, 2), { 128 });
    a3test::Stereo zeros { std::vector<float> (441000, 0.0f), std::vector<float> (441000, 0.0f) };
    const auto r = run (*strip, zeros, { 128 });
    const size_t tail = 44100;   // the filters' ring-out is over after the first second
    for (size_t i = zeros.l.size() - tail; i < zeros.l.size(); ++i)
        ASSERT_EQ (r.moving.l[i] + r.steady.l[i], 0.0f) << i;
}

// Pre taps after TRIM + EQ, post sums moving + steady after VOLUME: the split
// is level-neutral, so at VOLUME 0 dB both read the same.
TEST (ChannelStrip, PreAndPostMetersAgreeAtUnityVolume)
{
    auto strip = made (0.7f, 0.5f, 0.3f);
    run (*strip, a3test::sine (44100, 1000.0, 44100.0, 0.25f), { 128 });
    const auto m = strip->meters();
    for (int ch = 0; ch < 2; ++ch)
    {
        EXPECT_GT (m.prePeak[ch], 0.2f);
        EXPECT_NEAR (m.postPeak[ch], m.prePeak[ch], 1e-5f);
        EXPECT_NEAR (m.postRms[ch], m.preRms[ch], 1e-5f);
    }
}
