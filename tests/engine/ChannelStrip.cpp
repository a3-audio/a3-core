// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include "TestSignals.h"
#include "ChannelStrip.h"
#include "DjFilter.h"
#include "Isolator3.h"
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

// TRIM and EQ as made() sets them, built from the units directly.
a3test::Stereo trimmedAndEqd (const a3test::Stereo& in)
{
    a3::PurestGain trim;
    a3::SmoothEQ3 eq;
    trim.prepare (44100.0);
    eq.prepare (44100.0);
    trim.setParameters ((float) a3::laws::slopeVolume (1.0), 1.0f);
    const float top = (float) a3::laws::slopeEq (1.0);
    eq.setParameters (top, top, top);
    return a3test::runInBlocks (eq, a3test::runInBlocks (trim, in, 128), 128);
}

// Largest change between neighbouring samples of `changed - unchanged`: the
// step a control change adds on top of the signal's own slope.
double largestStep (const a3test::Stereo& changed, const a3test::Stereo& unchanged)
{
    double worst = 0.0;
    for (size_t i = 1; i < changed.l.size(); ++i)
    {
        const double now = (double) changed.l[i] - unchanged.l[i];
        const double before = (double) changed.l[i - 1] - unchanged.l[i - 1];
        worst = std::max (worst, std::abs (now - before));
    }
    return worst;
}

a3test::Stereo sum (const StripRun& r)
{
    a3test::Stereo s = r.moving;
    for (size_t i = 0; i < s.l.size(); ++i) { s.l[i] += r.steady.l[i]; s.r[i] += r.steady.r[i]; }
    return s;
}
}

// moving + steady = the EQ'd input, at every 3D/freq/Q, checked against TRIM
// and EQ built independently.
TEST (ChannelStrip, MovingPlusSteadyIsTheInputAtEveryPosition)
{
    const auto in = a3test::noise (22050, 0.25f, 9);
    const auto expected = trimmedAndEqd (in);
    for (float threeD : { 0.0f, 0.3f, 1.0f })
        for (float f : { 0.0f, 0.5f, 1.0f })
            for (float q : { 0.0f, 1.0f })
            {
                auto strip = made (threeD, f, q);
                const auto r = run (*strip, in, { 128 });
                EXPECT_LE (a3test::maxAbsDiff (sum (r), expected), 1e-6) << threeD << " " << f << " " << q;
            }
}

// The template's bed (n-multi-enc) takes the input straight, with no delay for
// Isolator3's frame: moving + steady is TRIM + EQ with nothing in between. At
// 3D just above 0 the band gain is A 0 = -40 dB (the law's floor): moving is
// 0.01 x Isolator3 (x). Exactly 0 is silence (ChannelStrip.ThreeDZeroSilencesTheMovingShare).
TEST (ChannelStrip, TheBedIsTheUndelayedInputAndTheBandFloorIsMinusFortyDb)
{
    const auto in = a3test::noise (22050, 0.25f, 3);
    auto strip = made (0.001f, 0.5f, 0.05f);
    const auto r = run (*strip, in, { 128 });

    const auto expected = trimmedAndEqd (in);
    EXPECT_LE (a3test::maxAbsDiff (sum (r), expected), 1e-6);

    a3::Isolator3 iso;
    iso.prepare (44100.0);
    iso.setParameters ((float) a3::laws::isolatorParam (0.5), (float) a3::laws::isolatorParam (0.05));
    auto band = a3test::runInBlocks (iso, expected, 128);
    for (auto* side : { &band.l, &band.r })
        for (auto& v : *side)
            v = (float) (0.01 * v);
    EXPECT_GT (*std::max_element (r.moving.l.begin(), r.moving.l.end()), 1e-4f);   // the band is there
    EXPECT_LE (a3test::maxAbsDiff (r.moving, band), 1e-7);
}

// Closing the band (3D 0.9 -> 0) glides with PurestGain, as REAPER does; no
// cut. Measured on a 1 kHz sine at -6 dBFS inside the band: the change adds at
// most ~0.064 between neighbouring samples (0.9 -> 0.011 probe; the band's own
// slope at 1 kHz is 0.071 per sample, times the gain still to travel), where a
// cut at the latch adds ~0.40. 0.1 sits between the two.
TEST (ChannelStrip, ClosingTheBandGlidesWithoutAStep)
{
    const float level = std::pow (10.0f, -6.0f / 20.0f);
    const auto in = a3test::sine (44100, 1000.0, 44100.0, level);
    auto runWith = [&] (bool close) {
        auto strip = made (0.9f, 0.5f, 0.05f);
        StripRun r { in, in };
        for (size_t s = 0; s < in.l.size(); s += 128)
        {
            if (close && s == 128 * 100)
                strip->controls().threeD = 0.0f;
            const int n = (int) std::min<size_t> (128, in.l.size() - s);
            const float* i[2] { in.l.data() + s, in.r.data() + s };
            float* m[2] { r.moving.l.data() + s, r.moving.r.data() + s };
            float* st[2] { r.steady.l.data() + s, r.steady.r.data() + s };
            strip->process (i, m, st, n);
        }
        return r;
    };
    const auto open = runWith (false), closing = runWith (true);
    EXPECT_GT (*std::max_element (open.moving.l.begin(), open.moving.l.end()), 0.3f);   // 1 kHz is in the band
    EXPECT_LE (largestStep (closing.moving, open.moving), 0.1);
    EXPECT_LE (largestStep (closing.steady, open.steady), 0.1);
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
    // 3D from 0 (-40 dB) to 0.9 and VOLUME, set mid-block in the odd run.
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
    {
        // each path on its own: a sum of two could hide +x and -x
        ASSERT_EQ (r.moving.l[i], 0.0f) << "moving.l " << i;
        ASSERT_EQ (r.moving.r[i], 0.0f) << "moving.r " << i;
        ASSERT_EQ (r.steady.l[i], 0.0f) << "steady.l " << i;
        ASSERT_EQ (r.steady.r[i], 0.0f) << "steady.r " << i;
        ASSERT_EQ (r.moving.l[i] + r.steady.l[i], 0.0f) << i;
    }
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

// The bench's ch{n}_pre outputs: process() can also write the post-TRIM+EQ
// signal, across chunk and block boundaries, and writing it changes nothing else.
TEST (ChannelStrip, TheOptionalPreOutputIsTrimAndEqAndChangesNothingElse)
{
    const auto in = a3test::noise (3000, 0.25f, 4);
    const auto expected = trimmedAndEqd (in);
    auto withPre = made (0.7f, 0.5f, 0.3f);
    auto without = made (0.7f, 0.5f, 0.3f);
    withPre->prepare (44100.0, 512);                  // 3000 frames in one call: several chunks
    without->prepare (44100.0, 512);
    a3test::Stereo pre { std::vector<float> (3000), std::vector<float> (3000) };
    a3test::Stereo m = in, s = in;
    const float* i[2] { in.l.data(), in.r.data() };
    float* mp[2] { m.l.data(), m.r.data() };
    float* sp[2] { s.l.data(), s.r.data() };
    float* pp[2] { pre.l.data(), pre.r.data() };
    withPre->process (i, mp, sp, 3000, pp);
    const auto reference = run (*without, in, { 3000 });
    EXPECT_LE (a3test::maxAbsDiff (pre, expected), 1e-6);
    EXPECT_EQ (a3test::maxAbsDiff (m, reference.moving), 0.0);
    EXPECT_EQ (a3test::maxAbsDiff (s, reference.steady), 0.0);
}

namespace
{
struct TapRun { a3test::Stereo moving, steady, movingPre, steadyPre, pre; };

// The StripBuffers path, in blocks of `block`.
TapRun runWithTaps (a3::ChannelStrip& strip, const a3test::Stereo& in, int block)
{
    TapRun r { in, in, in, in, in };
    for (size_t s = 0; s < in.l.size(); s += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.l.size() - s);
        const float* i[2] { in.l.data() + s, in.r.data() + s };
        a3::StripBuffers b;
        b.moving[0] = r.moving.l.data() + s;          b.moving[1] = r.moving.r.data() + s;
        b.steady[0] = r.steady.l.data() + s;          b.steady[1] = r.steady.r.data() + s;
        b.movingPreVolume[0] = r.movingPre.l.data() + s; b.movingPreVolume[1] = r.movingPre.r.data() + s;
        b.steadyPreVolume[0] = r.steadyPre.l.data() + s; b.steadyPreVolume[1] = r.steadyPre.r.data() + s;
        b.pre[0] = r.pre.l.data() + s;                b.pre[1] = r.pre.r.data() + s;
        strip.process (i, b, n);
    }
    return r;
}

struct FilterRig
{
    a3::FilterControls shared;
    std::unique_ptr<a3::ChannelStrip> strip = std::make_unique<a3::ChannelStrip>();
    FilterRig (bool on)
    {
        shared.kind = 0; shared.mode = 1; shared.resonance = 0.0f;
        shared.frequency = 0.62f;   // high-pass, ~1 kHz
        strip->setSharedFilter (&shared);
        strip->prepare (44100.0, 512);
        auto& c = strip->controls();
        c.gain = 1.0f; c.volume = 1.0f; c.eqHigh = c.eqMid = c.eqLow = 1.0f;
        c.threeD = 1.0f; c.frequency = 0.5f; c.q = 0.0f;
        c.filterOn = on ? 1.0f : 0.0f;
    }
};
}

namespace
{
// The filter alone on the strip's x, in the strip's 128 blocks, filter on from the start.
a3test::Stereo filteredLikeTheRig (const a3test::Stereo& x, const a3::FilterControls& controls)
{
    a3::DjFilter filter;
    filter.prepare (44100.0, 512, 2);
    a3test::Stereo out = x;
    for (size_t s = 0; s < x.l.size(); s += 128)
    {
        const int n = (int) std::min<size_t> (128, x.l.size() - s);
        float* io[2] { out.l.data() + s, out.r.data() + s };
        filter.process (io, 2, n, true, controls);
    }
    return out;
}
}

TEST (ChannelStrip, WithoutSharedFilterIsPlan2Exact)
{
    const auto in = a3test::noise (22050, 0.25f, 21);
    auto old = made (0.4f, 0.3f, 0.2f);
    const auto expected = run (*old, in, { 128 });
    auto strip = made (0.4f, 0.3f, 0.2f);
    strip->controls().filterOn = 1.0f;   // no filter attached: ignored
    const auto r = runWithTaps (*strip, in, 128);
    EXPECT_EQ (r.moving.l, expected.moving.l);
    EXPECT_EQ (r.steady.r, expected.steady.r);
}

TEST (ChannelStrip, FilterSitsAfterEqBeforeTheBand)
{
    const auto in = a3test::sine (22050, 100.0, 44100.0, 0.25f);
    FilterRig rig (true);
    const auto r = runWithTaps (*rig.strip, in, 128);

    const auto x = trimmedAndEqd (in);
    EXPECT_LE (a3test::maxAbsDiff (r.pre, x), 1e-6);   // pre tap: unfiltered

    const auto filtered = filteredLikeTheRig (x, rig.shared);
    EXPECT_LE (a3test::maxAbsDiff (sum ({ r.moving, r.steady }), filtered), 1e-6);   // moving + steady = filtered x
    EXPECT_LE (a3test::maxAbsDiff (sum ({ r.movingPre, r.steadyPre }), filtered), 1e-6);
    // the 100 Hz tone is well under the 1 kHz high-pass
    const auto peak = [] (const a3test::Stereo& s2) { return *std::max_element (s2.l.begin() + 11025, s2.l.end()); };
    EXPECT_LT (peak (filtered), 0.1f * peak (x));
}

TEST (ChannelStrip, PreVolumeTapsAreMovingAndSteadyBeforeVolume)
{
    const auto in = a3test::sine (22050, 3000.0, 44100.0, 0.25f);
    FilterRig rig (true);
    rig.strip->controls().volume = 0.0f;
    const auto r = runWithTaps (*rig.strip, in, 128);

    const auto peak = [] (const std::vector<float>& v) { return *std::max_element (v.begin() + 11025, v.end()); };
    // VOLUME 0 is silence (R18), not PurestGain's -40 dB floor.
    EXPECT_EQ (peak (r.moving.l), 0.0f);
    EXPECT_EQ (peak (r.steady.l), 0.0f);
    EXPECT_GT (peak (r.movingPre.l) + peak (r.steadyPre.l), 0.05f);

    const auto filtered = filteredLikeTheRig (trimmedAndEqd (in), rig.shared);
    EXPECT_LE (a3test::maxAbsDiff (sum ({ r.movingPre, r.steadyPre }), filtered), 1e-6);
}

// R18 / D23: every gain control at 0 is exact silence, reached through a 10 ms ramp.
namespace
{
a3test::Stereo noiseThenRun (a3::ChannelStrip& strip, size_t frames, unsigned seed, TapRun& taps)
{
    const auto in = a3test::noise (frames, 0.25f, seed);
    taps = runWithTaps (strip, in, 128);
    return in;
}
bool allZero (const std::vector<float>& v, size_t from)
{
    for (size_t i = from; i < v.size(); ++i)
        if (v[i] != 0.0f)
            return false;
    return true;
}
}

TEST (ChannelStrip, VolumeZeroIsSilence)
{
    auto strip = made (0.7f, 0.5f, 0.3f);
    strip->controls().volume = 0.0f;
    TapRun r;
    noiseThenRun (*strip, 22050, 5, r);
    for (auto* v : { &r.moving.l, &r.moving.r, &r.steady.l, &r.steady.r })
        EXPECT_TRUE (allZero (*v, 11025));
    const auto peak = [] (const std::vector<float>& v) { return *std::max_element (v.begin() + 11025, v.end()); };
    EXPECT_GT (peak (r.movingPre.l), 1e-3f);    // the pre-VOLUME taps carry signal
    EXPECT_GT (peak (r.steadyPre.l), 1e-3f);
    EXPECT_GT (peak (r.pre.l), 1e-3f);
}

TEST (ChannelStrip, GainZeroIsSilence)
{
    auto strip = made (0.7f, 0.5f, 0.3f);
    strip->controls().gain = 0.0f;
    TapRun r;
    noiseThenRun (*strip, 22050, 6, r);
    for (auto* v : { &r.moving.l, &r.moving.r, &r.steady.l, &r.steady.r, &r.pre.l, &r.pre.r })
        EXPECT_TRUE (allZero (*v, 11025));
}

TEST (ChannelStrip, GainZeroIsSilenceBehindTheFilterToo)
{
    FilterRig rig (true);
    rig.strip->controls().gain = 0.0f;
    TapRun r;
    noiseThenRun (*rig.strip, 22050, 7, r);
    for (auto* v : { &r.moving.l, &r.steady.l, &r.pre.l, &r.movingPre.l, &r.steadyPre.l })
        EXPECT_TRUE (allZero (*v, 11025));
}

// Isolator3 rings on after its input is zeroed; GAIN 0 must still silence every tap.
TEST (ChannelStrip, GainZeroSilencesThePreVolumeTapsDespiteTheIsolatorRingOut)
{
    auto strip = made (0.7f, 0.9f, 0.8f);
    const auto in = a3test::noise (44100, 0.25f, 11);
    TapRun r = runWithTaps (*strip, in, 128);   // GAIN 1 under signal: the isolator is ringing
    EXPECT_GT (*std::max_element (r.movingPre.l.begin() + 22050, r.movingPre.l.end()), 1e-3f);
    strip->controls().gain = 0.0f;
    r = runWithTaps (*strip, in, 128);
    for (auto* v : { &r.movingPre.l, &r.movingPre.r, &r.steadyPre.l, &r.steadyPre.r, &r.pre.l, &r.pre.r,
                     &r.moving.l, &r.steady.l })
        EXPECT_TRUE (allZero (*v, 4410));
}

TEST (ChannelStrip, ThreeDZeroSilencesTheMovingShare)
{
    auto strip = made (0.0f, 0.5f, 0.05f);
    TapRun r;
    const auto in = noiseThenRun (*strip, 22050, 8, r);
    EXPECT_TRUE (allZero (r.moving.l, 11025));
    EXPECT_TRUE (allZero (r.moving.r, 11025));
    EXPECT_TRUE (allZero (r.movingPre.l, 11025));
    // the steady share carries everything: it is the EQ'd input
    EXPECT_LE (a3test::maxAbsDiff (r.steady, trimmedAndEqd (in)), 1e-6);
}

// A control jumped 1 -> 0 under a sine adds no step beyond 2x the dry signal's own slope.
TEST (ChannelStrip, ReachingZeroDoesNotClick)
{
    const auto in = a3test::sine (44100, 440.0, 44100.0, 0.5f);
    double dry = 0.0;
    for (size_t i = 1; i < in.l.size(); ++i)
        dry = std::max (dry, (double) std::abs (in.l[i] - in.l[i - 1]));
    enum Which { Volume, Gain, ThreeD };
    for (Which which : { Volume, Gain, ThreeD })
    {
        auto strip = made (1.0f, 0.5f, 0.05f);
        a3test::Stereo mv = in, st = in, mpre = in, spre = in;
        for (size_t s = 0; s < in.l.size(); s += 128)
        {
            if (s == 128 * 100)
            {
                auto& c = strip->controls();
                (which == Volume ? c.volume : which == Gain ? c.gain : c.threeD) = 0.0f;
            }
            const int n = (int) std::min<size_t> (128, in.l.size() - s);
            const float* i[2] { in.l.data() + s, in.r.data() + s };
            float* m[2] { mv.l.data() + s, mv.r.data() + s };
            float* t[2] { st.l.data() + s, st.r.data() + s };
            a3::StripBuffers b;
            b.moving[0] = m[0]; b.moving[1] = m[1]; b.steady[0] = t[0]; b.steady[1] = t[1];
            b.movingPreVolume[0] = mpre.l.data() + s; b.movingPreVolume[1] = mpre.r.data() + s;
            b.steadyPreVolume[0] = spre.l.data() + s; b.steadyPreVolume[1] = spre.r.data() + s;
            strip->process (i, b, n);
        }
        double worst = 0.0;
        for (size_t i = 1; i < in.l.size(); ++i)
            for (auto* v : { &mv.l, &st.l, &mpre.l, &spre.l })
                worst = std::max (worst, (double) std::abs ((*v)[i] - (*v)[i - 1]));
        EXPECT_LE (worst, 2.0 * dry) << which;
        if (which != ThreeD)
            EXPECT_TRUE (allZero (mv.l, 128 * 100 + 4410)) << which;
        if (which == Gain)
            for (auto* v : { &mpre.l, &spre.l })
                EXPECT_TRUE (allZero (*v, 128 * 100 + 4410));
    }
}
