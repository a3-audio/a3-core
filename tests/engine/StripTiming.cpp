// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <string>
#include <vector>
#include "TestSignals.h"
#include "ChannelStrip.h"

// The guard against regressions; the worst case is the bench's job on the rig.
TEST (StripTiming, FourStripsUnderTwoTenthsOfAMillisecondPerBlock)
{
#ifndef NDEBUG
    GTEST_SKIP() << "timing is measured in Release only";
#endif
    std::array<a3::ChannelStrip, 4> strips;
    for (auto& s : strips) { s.prepare (44100.0, 128); s.controls().threeD = 0.7f; s.controls().gain = 0.8f; s.controls().volume = 0.8f; }
    const auto in = a3test::noise (128, 0.25f, 1);
    std::vector<float> ml (128), mr (128), sl (128), sr (128);
    const float* i[2] { in.l.data(), in.r.data() };
    float* m[2] { ml.data(), mr.data() }; float* st[2] { sl.data(), sr.data() };
    double worst = 0, total = 0; const int blocks = 20000;
    for (int b = 0; b < blocks; ++b)
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (auto& s : strips) s.process (i, m, st, 128);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        worst = std::max (worst, ms); total += ms;
    }
    const double mean = total / blocks;
    RecordProperty ("mean_ms", std::to_string (mean)); RecordProperty ("worst_ms", std::to_string (worst));
    std::printf ("StripTiming: mean_ms %.6f worst_ms %.6f\n", mean, worst);
    EXPECT_LT (mean, 0.2);   // the worst case is the bench's job on a3nuc1; here the mean guards regressions
}
