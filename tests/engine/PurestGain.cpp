// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <functional>
#include "TestSignals.h"
#include "reference/AirwindowsReference.h"
#include "PurestGain.h"

namespace
{
double compare (double sampleRate, std::function<void (a3::PurestGain&, a3ref::Reference&, size_t)> schedule)
{
    a3::PurestGain own;
    own.prepare (sampleRate);
    auto ref = a3ref::makePurestGain ((float) sampleRate);
    const auto in = a3test::noise ((size_t) sampleRate * 2, 0.5f, 7);
    auto ownOut = in, refOut = in;
    for (size_t s = 0; s < in.l.size(); s += 128)
    {
        const int n = (int) std::min<size_t> (128, in.l.size() - s);
        schedule (own, *ref, s);
        float* a[2] { ownOut.l.data() + s, ownOut.r.data() + s };
        float* b[2] { refOut.l.data() + s, refOut.r.data() + s };
        own.process (a, n);
        ref->process (b, n);
    }
    return a3test::maxAbsDiff (ownOut, refOut);
}
}

TEST (PurestGain, MatchesTheReferenceOverTheRigsRange)
{
    for (int i = 0; i <= 100; ++i)
    {
        const float a = 0.5f * (float) i / 100.0f;   // slope_volume and bandGain give 0..0.5
        EXPECT_LE (compare (44100.0, [a] (auto& o, auto& r, size_t) { o.setParameters (a, 1.0f); r.setParameter (0, a); r.setParameter (1, 1.0f); }), 1e-5) << "A " << a;
    }
}

TEST (PurestGain, FollowsAMovingFaderLikeTheReference)
{
    EXPECT_LE (compare (44100.0, [] (auto& o, auto& r, size_t s) {
        const float a = 0.5f * (float) ((s / 128) % 50) / 50.0f;   // a new value every block
        o.setParameters (a, 1.0f); r.setParameter (0, a); r.setParameter (1, 1.0f); }), 1e-5);
}

TEST (PurestGain, MatchesAt48k)    // review focus 3
{
    EXPECT_LE (compare (48000.0, [] (auto& o, auto& r, size_t) { o.setParameters (0.3f, 1.0f); r.setParameter (0, 0.3f); r.setParameter (1, 1.0f); }), 1e-5);
}

TEST (PurestGain, UnityIsBitExact)
{
    a3::PurestGain own; own.prepare (44100.0); own.setParameters (0.5f, 1.0f);
    const auto in = a3test::noise (4410, 0.9f, 3);
    EXPECT_EQ (a3test::maxAbsDiff (in, a3test::runInBlocks (own, in, 128)), 0.0);
}

// The control grid is 128 frames counted by sample position, whatever the
// call sizes. A parameter set mid-block takes effect at the next boundary.
TEST (PurestGain, SplitCallsMatchWholeBlocks)
{
    const int sizes[] { 1, 7, 120, 64, 64, 128, 300, 33, 255, 2, 126, 500 };
    const auto in = a3test::noise (20000, 0.5f, 11);
    const size_t total = in.l.size();

    // Plan the split calls; each sets a new value at its start.
    std::vector<size_t> starts;
    std::vector<float> values;
    for (size_t s = 0, k = 0; s < total; s += (size_t) sizes[k++ % 12])
    {
        starts.push_back (s);
        values.push_back (0.5f * (float) ((k * 37) % 50) / 50.0f);
    }

    a3::PurestGain split;
    split.prepare (44100.0);
    auto splitOut = in;
    for (size_t c = 0; c < starts.size(); ++c)
    {
        const size_t n = std::min ((size_t) sizes[c % 12], total - starts[c]);
        split.setParameters (values[c], 1.0f);
        float* io[2] { splitOut.l.data() + starts[c], splitOut.r.data() + starts[c] };
        split.process (io, (int) n);
    }

    // Value that block k latches: the last one set at a call start <= 128k.
    auto valueForBlock = [&] (size_t k) {
        size_t c = 0;
        while (c + 1 < starts.size() && starts[c + 1] <= 128 * k) ++c;
        return values[c];
    };
    a3::PurestGain whole;
    whole.prepare (44100.0);
    auto wholeOut = in;
    for (size_t s = 0; s < total; s += 128)
    {
        whole.setParameters (valueForBlock (s / 128), 1.0f);
        float* io[2] { wholeOut.l.data() + s, wholeOut.r.data() + s };
        whole.process (io, (int) std::min<size_t> (128, total - s));
    }
    EXPECT_EQ (a3test::maxAbsDiff (splitOut, wholeOut), 0.0);
}
