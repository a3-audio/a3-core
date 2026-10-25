// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "DjFilter.h"
#include "Laws.h"
#include "TestSignals.h"

namespace
{
constexpr double rate = 44100.0;

// The control value whose cutoff is `hz`, found by a scan over the law.
float controlAt (bool highPass, double hz)
{
    float best = 0.0f;
    double bestError = 1e9;
    for (int i = 0; i <= 10000; ++i)
    {
        const double c = i / 10000.0;
        const double error = std::abs (std::log (a3::laws::filterCutoffHz (highPass, c) / hz));
        if (error < bestError)
            bestError = error, best = (float) c;
    }
    return best;
}

void setUp (a3::FilterControls& c, int kind, bool highPass, double hz, float resonance = 0.0f)
{
    c.kind = kind;
    c.mode = highPass ? 1 : 0;
    c.frequency = controlAt (highPass, hz);
    c.resonance = resonance;
}

struct Multi
{
    std::vector<std::vector<float>> ch;
    std::vector<float*> ptr (size_t offset)
    {
        std::vector<float*> p;
        for (auto& c : ch)
            p.push_back (c.data() + offset);
        return p;
    }
};

Multi runFilter (a3::DjFilter& f, Multi in, const a3::FilterControls& c, bool on, int block)
{
    const size_t total = in.ch[0].size();
    for (size_t s = 0; s < total; s += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, total - s);
        auto p = in.ptr (s);
        f.process (p.data(), (int) p.size(), n, on, c);
    }
    return in;
}

Multi fromSine (int channels, double hz, size_t frames, float level = 0.5f)
{
    const auto s = a3test::sine (frames, hz, rate, level);
    return { std::vector<std::vector<float>> ((size_t) channels, s.l) };
}

double rmsTail (const std::vector<float>& x, size_t from)
{
    double sum = 0.0;
    for (size_t i = from; i < x.size(); ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / (double) (x.size() - from));
}
}

TEST (DjFilter, HighPassCutsLowsLowPassCutsHighs)
{
    for (int kind : { 0, 1 })
        for (bool highPass : { true, false })
        {
            const double stopHz = highPass ? 100.0 : 10000.0;
            const double passHz = highPass ? 10000.0 : 100.0;
            a3::FilterControls c;
            setUp (c, kind, highPass, 1000.0);
            const size_t frames = 44100;
            double stopped = 0.0, passed = 0.0;
            for (double hz : { stopHz, passHz })
            {
                a3::DjFilter f;
                f.prepare (rate, 512, 2);
                const auto in = fromSine (2, hz, frames);
                const auto out = runFilter (f, in, c, true, 512);
                const double ratio = rmsTail (out.ch[0], 22050) / rmsTail (in.ch[0], 22050);
                (hz == stopHz ? stopped : passed) = ratio;
            }
            EXPECT_LE (20.0 * std::log10 (stopped), -20.0) << "kind " << kind << " hp " << highPass;
            EXPECT_GT (20.0 * std::log10 (passed), -6.0) << "kind " << kind << " hp " << highPass;
        }
}

TEST (DjFilter, OffIsTransparent)
{
    for (int kind : { 0, 1 })
    {
        a3::FilterControls c;
        setUp (c, kind, true, 1000.0, 0.5f);
        a3::DjFilter f;
        f.prepare (rate, 512, 2);
        const auto in = fromSine (2, 440.0, 20000);
        const auto out = runFilter (f, in, c, false, 256);
        double worst = 0.0;
        for (size_t i = 0; i < in.ch[0].size(); ++i)
            worst = std::max (worst, (double) std::abs (out.ch[0][i] - in.ch[0][i]));
        EXPECT_LE (worst, 1e-7);

        // on, then off again: transparent after the crossfade
        a3::DjFilter g;
        g.prepare (rate, 512, 2);
        auto buf = fromSine (2, 440.0, 20000);
        auto p = buf.ptr (0);
        g.process (p.data(), 2, 5000, true, c);
        g.process (p.data() + 0, 2, 0, true, c);
        auto q = buf.ptr (5000);
        g.process (q.data(), 2, 15000, false, c);
        const auto ref = fromSine (2, 440.0, 20000);
        for (size_t i = 8000; i < 20000; ++i)
            ASSERT_NEAR (buf.ch[0][i], ref.ch[0][i], 1e-7) << i;
    }
}

TEST (DjFilter, SwitchingIsClickFree)
{
    const size_t frames = 44100;
    const auto dry = fromSine (2, 200.0, frames, 0.5f);
    double dryStep = 0.0;
    for (size_t i = 1; i < frames; ++i)
        dryStep = std::max (dryStep, (double) std::abs (dry.ch[0][i] - dry.ch[0][i - 1]));

    a3::FilterControls c;
    setUp (c, 0, false, 8000.0);   // a low-pass far above the sine: passes it with little change
    a3::DjFilter f;
    f.prepare (rate, 512, 2);
    auto buf = dry;
    const int block = 100;
    for (size_t s = 0; s < frames; s += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, frames - s);
        // toggles: on at block 10, kind at 20, off at 30..40, on again, kind back at 60
        const size_t b = s / (size_t) block;
        const bool on = ! (b >= 30 && b < 40);
        c.kind = (b >= 20 && b < 60) ? 1 : 0;
        auto p = buf.ptr (s);
        f.process (p.data(), 2, n, on && b >= 10, c);
    }
    double step = 0.0;
    for (size_t i = 1; i < frames; ++i)
        step = std::max (step, (double) std::abs (buf.ch[0][i] - buf.ch[0][i - 1]));
    EXPECT_LE (step, 2.0 * dryStep);
}

TEST (DjFilter, AnyBlockSize)
{
    for (int kind : { 0, 1 })
    {
        a3::FilterControls c;
        setUp (c, kind, true, 1000.0, 0.4f);
        const auto in = fromSine (2, 700.0, 30000);
        Multi reference;
        for (int block : { 64, 128, 1000, 512 })
        {
            a3::DjFilter f;
            f.prepare (rate, 512, 2);
            // toggle on after a while so the crossfade and the latch grid are in play
            Multi buf = in;
            size_t s = 0;
            while (s < buf.ch[0].size())
            {
                const int n = (int) std::min<size_t> ((size_t) block, buf.ch[0].size() - s);
                auto p = buf.ptr (s);
                f.process (p.data(), 2, n, s >= 1000, c);
                s += (size_t) n;
            }
            if (reference.ch.empty())
                reference = buf;
            else
                for (size_t i = 0; i < buf.ch[0].size(); ++i)
                    ASSERT_NEAR (buf.ch[0][i], reference.ch[0][i], 1e-6) << "block " << block << " at " << i;
        }
    }
}

TEST (DjFilter, SixteenChannelsAreIdentical)
{
    for (int kind : { 0, 1 })
    {
        a3::FilterControls c;
        setUp (c, kind, true, 1000.0, 0.3f);

        a3::DjFilter wide;
        wide.prepare (rate, 512, 16);
        const auto same = fromSine (16, 300.0, 20000);
        const auto out = runFilter (wide, same, c, true, 300);
        for (int ch = 1; ch < 16; ++ch)
            EXPECT_EQ (out.ch[(size_t) ch], out.ch[0]) << "channel " << ch;

        // different signals: each equals the 2-channel result of its own signal
        Multi mixed;
        for (int ch = 0; ch < 16; ++ch)
            mixed.ch.push_back (a3test::sine (20000, 100.0 + 90.0 * ch, rate, 0.4f).l);
        a3::DjFilter wide2;
        wide2.prepare (rate, 512, 16);
        const auto outWide = runFilter (wide2, mixed, c, true, 300);
        for (int ch = 0; ch < 16; ++ch)
        {
            a3::DjFilter narrow;
            narrow.prepare (rate, 512, 2);
            Multi two { { mixed.ch[(size_t) ch], mixed.ch[(size_t) ch] } };
            const auto outNarrow = runFilter (narrow, two, c, true, 300);
            EXPECT_EQ (outWide.ch[(size_t) ch], outNarrow.ch[0]) << "channel " << ch;
        }
    }
}
