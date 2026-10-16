// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "TestSignals.h"
#include "Meters.h"

namespace
{
const float minusSixDb = std::pow (10.0f, -6.0f / 20.0f);

// Pushes `in` in calls of the given sizes; after each call, `afterCall` sees the meter.
template <typename AfterCall>
void push (a3::Meters& meters, const a3test::Stereo& in, const std::vector<int>& sizes, AfterCall afterCall)
{
    size_t s = 0, k = 0;
    while (s < in.l.size())
    {
        const int n = (int) std::min<size_t> ((size_t) sizes[k++ % sizes.size()], in.l.size() - s);
        const float* io[2] { in.l.data() + s, in.r.data() + s };
        meters.push (io, n);
        s += (size_t) n;
        afterCall (s);
    }
}
}

// A 1 kHz sine at -6 dBFS: peak 0.501, RMS 0.354 (within 1 %). The analyzer
// measures RMS over one period (128 frames on the rig), which holds 2.9 cycles
// of 1 kHz, so each window reads up to 1.4 % high: the readings' mean is held
// to 1 %, each single reading to 2 %.
TEST (Meters, SineAtMinusSixDb)
{
    a3::Meters meters;
    meters.prepare (44100.0);
    const auto in = a3test::sine (44100, 1000.0, 44100.0, minusSixDb);
    const double rmsExpected = minusSixDb / std::sqrt (2.0);
    double rmsSum = 0;
    int readings = 0;
    push (meters, in, { 128 }, [&] (size_t s) {
        if (s < 4410)
            return;   // after the window and the attack
        for (int ch = 0; ch < 2; ++ch)
        {
            EXPECT_NEAR (meters.peak (ch), minusSixDb, 0.01 * minusSixDb) << s;
            EXPECT_NEAR (meters.rms (ch), rmsExpected, 0.02 * rmsExpected) << s;
        }
        rmsSum += meters.rms (0);
        ++readings;
    });
    EXPECT_NEAR (meters.peak (0), 0.501, 0.005);
    EXPECT_NEAR (rmsSum / readings, 0.354, 0.00354);
}

// Silence: the RMS falls by the analyzer's release (VU_RMS_RELEASE 0.2) per
// window, the peak reads the window's own maximum, 0.
TEST (Meters, SilenceDecaysWithTheAnalyzersRelease)
{
    a3::Meters meters;
    meters.prepare (44100.0);
    push (meters, a3test::sine (128 * 35, 1000.0, 44100.0, minusSixDb), { 128 }, [] (size_t) {});
    const a3test::Stereo zeros { std::vector<float> (128 * 10, 0.0f), std::vector<float> (128 * 10, 0.0f) };
    float last = meters.rms (0);
    ASSERT_GT (last, 0.3f);
    push (meters, zeros, { 128 }, [&] (size_t) {
        EXPECT_NEAR (meters.rms (0), last * 0.8f, 1e-6f);
        EXPECT_EQ (meters.peak (0), 0.0f);
        last = meters.rms (0);
    });
}

// The window is 128 frames counted across calls, so the reading after a whole
// window does not depend on how the frames arrived.
TEST (Meters, WindowIsCarriedAcrossCalls)
{
    a3::Meters whole, split;
    whole.prepare (44100.0);
    split.prepare (44100.0);
    const auto in = a3test::noise (128 * 30, 0.4f, 8);
    push (whole, in, { 128 }, [] (size_t) {});
    push (split, in, { 1, 7, 120, 64, 64, 128 }, [] (size_t) {});
    for (int ch = 0; ch < 2; ++ch)
    {
        EXPECT_EQ (whole.rms (ch), split.rms (ch));
        EXPECT_EQ (whole.peak (ch), split.peak (ch));
    }
}
