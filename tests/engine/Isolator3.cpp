// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <algorithm>
#include <functional>
#include <vector>
#include "TestSignals.h"
#include "reference/AirwindowsReference.h"
#include "Isolator3.h"
#include "Laws.h"

namespace
{
// The plugin's parameter order: 0 Iso (frequency), 1 Q.
double compare (double sr, const std::function<void (size_t, float&, float&)>& params, size_t frames)
{
    a3::Isolator3 own;
    own.prepare (sr);
    auto ref = a3ref::makeIsolator3 ((float) sr);
    const auto in = a3test::noise (frames, 0.14f, 5);   // about -17 dBFS, as the spike
    auto o = in, r = in;
    for (size_t s = 0; s < frames; s += 128)
    {
        const int n = (int) std::min<size_t> (128, frames - s);
        float iso, q;
        params (s, iso, q);
        own.setParameters (iso, q);
        ref->setParameter (0, iso);
        ref->setParameter (1, q);
        float* a[2] { o.l.data() + s, o.r.data() + s };
        float* b[2] { r.l.data() + s, r.r.data() + s };
        own.process (a, n);
        ref->process (b, n);
    }
    return a3test::maxAbsDiff (o, r);
}
}

// 180 static points over the rig's range, compared from sample 0: the
// start-up burst at high Iso is part of what must match.
TEST (Isolator3, MatchesTheReferenceOnStaticPoints)
{
    for (int f = 0; f < 18; ++f)
        for (int qi = 0; qi < 10; ++qi)
        {
            const float iso = (float) a3::laws::isolatorParam (f / 17.0);
            const float q = (float) a3::laws::isolatorParam (qi / 9.0);
            EXPECT_LE (compare (44100.0, [=] (size_t, float& i, float& qq) { i = iso; qq = q; }, 22050), 1e-5)
                << "iso " << iso << " q " << q;
        }
}

// Copied, not smoothed (decision 5): a hand-speed sweep matches the original, bursts included.
// A whole number of 128-frame blocks (689, about 2 s): the original glides
// across each host call, the rebuild across the 128-frame grid (ruling R3),
// so a short trailing call under moving parameters would differ by design.
TEST (Isolator3, MovesLikeTheReferenceBurstsIncluded)
{
    EXPECT_LE (compare (44100.0, [] (size_t s, float& i, float& q) {
        i = (float) a3::laws::isolatorParam ((double) (s % 44100) / 44100.0); q = 0.05f; }, 689 * 128), 1e-5);
}

TEST (Isolator3, MatchesAt48k)
{
    EXPECT_LE (compare (48000.0, [] (size_t, float& i, float& q) { i = 0.4f; q = 0.3f; }, 24000), 1e-5);
}

// The control grid is 128 frames counted by sample position, whatever the
// call sizes; the coefficient glide runs across each 128-frame block. A value
// set mid-block takes effect at the next boundary.
TEST (Isolator3, SplitCallsMatchWholeBlocks)
{
    const int sizes[] { 1, 7, 120, 64, 64, 128, 300, 33, 255, 2, 126, 500 };
    const auto in = a3test::noise (20000, 0.5f, 11);
    const size_t total = in.l.size();
    const float levels[] { 0.05f, 0.9f, 0.3f, 0.5f, 0.1f, 0.7f, 0.2f };

    std::vector<size_t> starts;
    std::vector<float> iso, q;
    for (size_t s = 0, k = 0; s < total; s += (size_t) sizes[k++ % 12])
    {
        starts.push_back (s);
        iso.push_back (levels[(k * 3) % 7]);
        q.push_back (levels[(k * 5 + 1) % 7]);
    }

    a3::Isolator3 split;
    split.prepare (44100.0);
    auto splitOut = in;
    for (size_t c = 0; c < starts.size(); ++c)
    {
        const size_t n = std::min ((size_t) sizes[c % 12], total - starts[c]);
        split.setParameters (iso[c], q[c]);
        float* io[2] { splitOut.l.data() + starts[c], splitOut.r.data() + starts[c] };
        split.process (io, (int) n);
    }

    // What block k latches: the last values set at a call start <= 128k.
    auto indexForBlock = [&] (size_t k) {
        size_t c = 0;
        while (c + 1 < starts.size() && starts[c + 1] <= 128 * k) ++c;
        return c;
    };
    a3::Isolator3 whole;
    whole.prepare (44100.0);
    auto wholeOut = in;
    for (size_t s = 0; s < total; s += 128)
    {
        const size_t c = indexForBlock (s / 128);
        whole.setParameters (iso[c], q[c]);
        float* io[2] { wholeOut.l.data() + s, wholeOut.r.data() + s };
        whole.process (io, (int) std::min<size_t> (128, total - s));
    }
    EXPECT_EQ (a3test::maxAbsDiff (splitOut, wholeOut), 0.0);
}

TEST (Isolator3, ReportsOneSampleOfLatency) { EXPECT_EQ (a3::Isolator3::latencySamples, 1); }
