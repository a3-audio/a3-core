// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "TestSignals.h"
#include "reference/AirwindowsReference.h"
#include "SmoothEQ3.h"
#include "Laws.h"

namespace
{
// The plugin's parameter order: 0 High, 1 Mid, 2 Bass.
double compareAt (double sr, float t, float m, float b, size_t frames)
{
    a3::SmoothEQ3 own;
    own.prepare (sr);
    own.setParameters (t, m, b);
    auto ref = a3ref::makeSmoothEQ3 ((float) sr);
    ref->setParameter (0, t);
    ref->setParameter (1, m);
    ref->setParameter (2, b);
    const auto in = a3test::noise (frames, 0.25f, 11);
    return a3test::maxAbsDiff (a3test::runInBlocks (own, in, 128), a3test::runInBlocks (*ref, in, 128));
}
}

// The desk's range: slope_eq gives 0..~0.87 (kill .. +6 dB). 16 steps per band.
TEST (SmoothEQ3, MatchesTheReferenceOnTheDesksGrid)
{
    const auto v = [] (int i) { return (float) a3::laws::slopeEq (i / 15.0); };
    for (int t = 0; t < 16; ++t)
        for (int m = 0; m < 16; ++m)
            for (int b = 0; b < 16; ++b)
                EXPECT_LE (compareAt (44100.0, v (t), v (m), v (b), 22050), 1e-5) << t << " " << m << " " << b;
}

TEST (SmoothEQ3, MatchesAt48k) { EXPECT_LE (compareAt (48000.0, 0.2f, 0.55f, 0.6f, 24000), 1e-5); }

// The gains jump at block starts (no smoothing), so the reference must follow
// when they change between blocks, state carried on.
TEST (SmoothEQ3, FollowsParameterChangesBetweenBlocks)
{
    a3::SmoothEQ3 own;
    own.prepare (44100.0);
    auto ref = a3ref::makeSmoothEQ3 (44100.0f);
    const auto in = a3test::noise (44100, 0.25f, 5);
    const float values[] { 0.0f, 0.6f, 0.3f, 0.5f, 0.1f };
    auto set = [&] (size_t start) {
        const size_t k = start / 128;
        const float t = values[k % 5], m = values[(k / 5) % 5], b = values[(k / 3) % 5];
        own.setParameters (t, m, b);
        ref->setParameter (0, t);
        ref->setParameter (1, m);
        ref->setParameter (2, b);
    };
    EXPECT_LE (a3test::maxAbsDiff (a3test::runInBlocks (own, in, 128, set),
                                   a3test::runInBlocks (*ref, in, 128, set)), 1e-5);
}

// The control grid is 128 frames counted by sample position, whatever the
// call sizes. A parameter set mid-block takes effect at the next boundary.
TEST (SmoothEQ3, SplitCallsMatchWholeBlocks)
{
    const int sizes[] { 1, 7, 120, 64, 64, 128, 300, 33, 255, 2, 126, 500 };
    const auto in = a3test::noise (20000, 0.5f, 11);
    const size_t total = in.l.size();
    const float levels[] { 0.0f, 0.6f, 0.3f, 0.5f, 0.1f, 0.45f, 0.2f };

    std::vector<size_t> starts;
    std::vector<float> t, m, b;
    for (size_t s = 0, k = 0; s < total; s += (size_t) sizes[k++ % 12])
    {
        starts.push_back (s);
        t.push_back (levels[(k * 3) % 7]);
        m.push_back (levels[(k * 5 + 1) % 7]);
        b.push_back (levels[(k * 2 + 4) % 7]);
    }

    a3::SmoothEQ3 split;
    split.prepare (44100.0);
    auto splitOut = in;
    for (size_t c = 0; c < starts.size(); ++c)
    {
        const size_t n = std::min ((size_t) sizes[c % 12], total - starts[c]);
        split.setParameters (t[c], m[c], b[c]);
        float* io[2] { splitOut.l.data() + starts[c], splitOut.r.data() + starts[c] };
        split.process (io, (int) n);
    }

    // What block k latches: the last values set at a call start <= 128k.
    auto indexForBlock = [&] (size_t k) {
        size_t c = 0;
        while (c + 1 < starts.size() && starts[c + 1] <= 128 * k) ++c;
        return c;
    };
    a3::SmoothEQ3 whole;
    whole.prepare (44100.0);
    auto wholeOut = in;
    for (size_t s = 0; s < total; s += 128)
    {
        const size_t c = indexForBlock (s / 128);
        whole.setParameters (t[c], m[c], b[c]);
        float* io[2] { wholeOut.l.data() + s, wholeOut.r.data() + s };
        whole.process (io, (int) std::min<size_t> (128, total - s));
    }
    EXPECT_EQ (a3test::maxAbsDiff (splitOut, wholeOut), 0.0);
}

// What the knob does to the sound: all three bands at one control scale the signal by eqDb (control).
// With equal band gains SmoothEQ3 is a plain gain, so the output is the input times that gain.
TEST (SmoothEQ3, KnobGivesTheIsolatorCurveInDb)
{
    const auto in = a3test::noise (4096, 0.25f, 5);
    for (double control : { 0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.75, 0.9, 1.0 })
    {
        a3::SmoothEQ3 eq;
        eq.prepare (44100.0);
        const auto p = (float) a3::laws::slopeEq (control);
        eq.setParameters (p, p, p);
        const auto out = a3test::runInBlocks (eq, in, 128);
        const double gain = std::pow (10.0, a3::laws::eqDb (control) / 20.0);
        double worst = 0.0;
        for (size_t i = 0; i < in.l.size(); ++i)
            worst = std::max (worst, std::abs (out.l[i] - gain * in.l[i]));
        EXPECT_LE (worst, 1e-5) << "control " << control << " (" << a3::laws::eqDb (control) << " dB)";
    }
}
