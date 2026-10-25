// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <random>
#include <vector>
#include "DeckStrip.h"
#include "Laws.h"

namespace
{
constexpr int ch16 = 16;
constexpr double rate = 44100.0;
constexpr float neutralEq = 0.4f;   // slopeEq (0.4) = 0.5, the flat EQ (not the control midpoint)
using Planes = std::vector<std::vector<float>>;   // [channel][frame]

Planes zeros (size_t frames) { return Planes (ch16, std::vector<float> (frames, 0.0f)); }

Planes noise16 (size_t frames, float level, uint32_t seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-level, level);
    auto p = zeros (frames);
    for (auto& plane : p)
        for (auto& v : plane)
            v = dist (rng);
    return p;
}

struct DeckRun { Planes pre, post; };

// Runs `in` through the strip in blocks of the given sizes (cycled).
DeckRun run (a3::DeckStrip& strip, const Planes& in, const a3::StripControls& controls,
             const std::vector<int>& blockSizes = { 512 })
{
    const size_t total = in[0].size();
    DeckRun r { zeros (total), zeros (total) };
    size_t s = 0, k = 0;
    while (s < total)
    {
        const int n = (int) std::min<size_t> ((size_t) blockSizes[k++ % blockSizes.size()], total - s);
        const float* i[ch16];
        a3::DeckTaps taps {};
        for (int c = 0; c < ch16; ++c)
        {
            i[c] = in[(size_t) c].data() + s;
            taps.preVolume[c] = r.pre[(size_t) c].data() + s;
            taps.postVolume[c] = r.post[(size_t) c].data() + s;
        }
        strip.process (i, taps, n, controls);
        s += (size_t) n;
    }
    return r;
}

void setNeutral (a3::StripControls& c)
{
    c.gain = 1.0f; c.volume = 1.0f;     // both 0 dB
    c.eqHigh = c.eqMid = c.eqLow = neutralEq;
    c.filterOn = 0.0f;
}

struct Fixture
{
    a3::FilterControls filter;
    a3::StripControls controls;
    a3::DeckStrip strip;
    explicit Fixture (int maxBlock = 512)
    {
        setNeutral (controls);
        strip.setSharedFilter (&filter);
        strip.prepare (rate, maxBlock);
    }
};

float maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    float m = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        m = std::max (m, std::abs (a[i] - b[i]));
    return m;
}

bool allZero (const std::vector<float>& v, size_t from)
{
    return std::all_of (v.begin() + (long) from, v.end(), [] (float x) { return x == 0.0f; });
}

float peakAfter (const std::vector<float>& v, size_t from)
{
    float m = 0.0f;
    for (size_t i = from; i < v.size(); ++i)
        m = std::max (m, std::abs (v[i]));
    return m;
}
}

TEST (DeckStrip, NeutralIsTransparent)
{
    Fixture f;
    const auto in = noise16 (22050, 0.5f, 1);
    const auto out = run (f.strip, in, f.controls);
    for (size_t c = 0; c < ch16; ++c)
    {
        EXPECT_LT (maxAbsDiff (out.post[c], in[c]), 1e-6f) << "channel " << c;
        EXPECT_LT (maxAbsDiff (out.pre[c], in[c]), 1e-6f) << "channel " << c;
    }
}

TEST (DeckStrip, ControlsActIdenticallyOnAllSixteen)
{
    const auto impulseOn = [] (size_t acn)
    {
        auto p = zeros (8192);
        p[acn][10] = 1.0f;
        return p;
    };
    const auto configure = [] (Fixture& f)
    {
        f.controls.gain = 0.8f;
        f.controls.volume = 0.9f;
        f.controls.eqHigh = 0.9f; f.controls.eqMid = 0.2f; f.controls.eqLow = 0.8f;
        f.controls.filterOn = 1.0f;
        f.filter.mode = 1;
        f.filter.frequency = 0.6f;
    };
    Fixture a, b;
    configure (a);
    configure (b);
    const auto outA = run (a.strip, impulseOn (0), a.controls);
    const auto outB = run (b.strip, impulseOn (7), b.controls);
    ASSERT_GT (peakAfter (outA.post[0], 0), 1e-3f);
    EXPECT_LT (maxAbsDiff (outA.post[0], outB.post[7]), 1e-7f);
    EXPECT_LT (maxAbsDiff (outA.pre[0], outB.pre[7]), 1e-7f);
    for (size_t c = 0; c < ch16; ++c)
        if (c != 7)
            EXPECT_EQ (peakAfter (outB.post[c], 0), 0.0f) << "leak into " << c;
}

TEST (DeckStrip, PlaneWaveKeepsItsDirection)
{
    // First order is the exact SN3D encoding of az 60 / el 20 (ACN 1 = Y, 2 = Z, 3 = X);
    // the higher orders are a fixed table: the strip is linear and identical per channel,
    // so any coefficient vector has to keep its shape.
    constexpr double az = 60.0 * M_PI / 180.0, el = 20.0 * M_PI / 180.0;
    std::array<double, ch16> coeff {};
    coeff[0] = 1.0;
    coeff[1] = std::sin (az) * std::cos (el);
    coeff[2] = std::sin (el);
    coeff[3] = std::cos (az) * std::cos (el);
    for (int c = 4; c < ch16; ++c)
        coeff[(size_t) c] = 0.7 * std::sin (1.3 * c + 0.4);

    Fixture f;
    f.controls.eqHigh = 0.9f; f.controls.eqMid = 0.3f; f.controls.eqLow = 0.7f;
    f.controls.filterOn = 1.0f;
    f.filter.mode = 1;
    f.filter.frequency = 0.5f;
    f.filter.kind = (int) a3::FilterKind::svf;
    const size_t frames = 44100;
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> dist (-0.1f, 0.1f);
    auto in = zeros (frames);
    for (size_t i = 0; i < frames; ++i)
    {
        const float s = dist (rng);
        for (size_t c = 0; c < ch16; ++c)
            in[c][i] = (float) coeff[c] * s;
    }
    const auto out = run (f.strip, in, f.controls);
    const size_t settled = 22050;
    ASSERT_GT (peakAfter (out.post[0], settled), 1e-3f);
    for (size_t c = 1; c < ch16; ++c)
        for (size_t i = settled; i < frames; ++i)
            ASSERT_NEAR (out.post[c][i], coeff[c] * out.post[0][i], 1e-5) << "channel " << c << " frame " << i;
}

TEST (DeckStrip, VolumeZeroIsSilenceOnAllSixteen)
{
    Fixture f;
    const auto in = noise16 (44100, 0.5f, 2);
    f.controls.volume = 0.0f;
    const auto out = run (f.strip, in, f.controls);
    const size_t settled = 2048;
    for (size_t c = 0; c < ch16; ++c)
    {
        EXPECT_TRUE (allZero (out.post[c], settled)) << "post, channel " << c;
        EXPECT_GT (peakAfter (out.pre[c], settled), 0.1f) << "pre, channel " << c;
    }
}

TEST (DeckStrip, GainZeroIsSilenceOnAllSixteen)
{
    Fixture f;
    f.controls.filterOn = 1.0f;
    const auto in = noise16 (44100, 0.5f, 3);
    const auto before = run (f.strip, in, f.controls);
    ASSERT_GT (peakAfter (before.post[5], 0), 0.1f);
    f.controls.gain = 0.0f;
    const auto out = run (f.strip, in, f.controls);
    const size_t settled = 2048;
    for (size_t c = 0; c < ch16; ++c)
    {
        EXPECT_TRUE (allZero (out.pre[c], settled)) << "pre, channel " << c;
        EXPECT_TRUE (allZero (out.post[c], settled)) << "post, channel " << c;
    }
    // and a strip that starts at 0 is silent from the first frame
    Fixture g;
    g.controls.gain = 0.0f;
    const auto fromStart = run (g.strip, in, g.controls);
    for (size_t c = 0; c < ch16; ++c)
        EXPECT_TRUE (allZero (fromStart.post[c], 0)) << "channel " << c;
}

TEST (DeckStrip, MetersReadW)
{
    Fixture f;
    auto in = zeros (22050);
    in[3] = noise16 (22050, 0.5f, 4)[3];
    run (f.strip, in, f.controls);
    EXPECT_EQ (f.strip.preMeterW().peak, 0.0f);
    EXPECT_EQ (f.strip.preMeterW().rms, 0.0f);
    EXPECT_EQ (f.strip.postMeterW().peak, 0.0f);
    EXPECT_EQ (f.strip.postMeterW().rms, 0.0f);

    in = zeros (22050);
    in[0] = noise16 (22050, 0.5f, 4)[0];
    run (f.strip, in, f.controls);
    EXPECT_GT (f.strip.preMeterW().peak, 0.4f);
    EXPECT_GT (f.strip.preMeterW().rms, 0.1f);
    EXPECT_GT (f.strip.postMeterW().peak, 0.4f);
    EXPECT_GT (f.strip.postMeterW().rms, 0.1f);
}

TEST (DeckStrip, AnyBlockSize)
{
    const auto in = noise16 (30000, 0.3f, 6);
    const auto configure = [] (Fixture& f)
    {
        f.controls.eqHigh = 0.8f; f.controls.eqLow = 0.3f;
        f.controls.filterOn = 1.0f;
        f.filter.frequency = 0.4f;
    };
    Fixture reference;
    configure (reference);
    const auto expected = run (reference.strip, in, reference.controls, { 512 });
    for (const auto& sizes : { std::vector<int> { 64 }, { 2048 }, { 1, 127, 129, 700 }, { 5000 } })   // 5000 > prepared 512
    {
        Fixture f;
        configure (f);
        const auto got = run (f.strip, in, f.controls, sizes);
        for (size_t c = 0; c < ch16; ++c)
            EXPECT_LT (maxAbsDiff (got.post[c], expected.post[c]), 1e-6f) << "block " << sizes[0] << " channel " << c;
    }
}

TEST (DeckStrip, ResetClearsState)
{
    auto impulse = zeros (4096);
    impulse[2][0] = 1.0f;
    const auto configure = [] (Fixture& f)
    {
        f.controls.eqHigh = 0.9f; f.controls.eqLow = 0.1f;
        f.controls.filterOn = 1.0f;
        f.filter.frequency = 0.5f;
    };
    Fixture fresh;
    configure (fresh);
    const auto expected = run (fresh.strip, impulse, fresh.controls);

    Fixture used;
    configure (used);
    run (used.strip, noise16 (10000, 0.5f, 7), used.controls);
    used.strip.reset();
    const auto got = run (used.strip, impulse, used.controls);
    for (size_t c = 0; c < ch16; ++c)
        EXPECT_LT (maxAbsDiff (got.post[c], expected.post[c]), 1e-7f) << "channel " << c;
}
