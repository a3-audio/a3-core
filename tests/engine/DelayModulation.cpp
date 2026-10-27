// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include "DelayModulation.h"

namespace
{
constexpr double twoPi = 6.283185307179586;

std::unique_ptr<a3::IemNode> makeDualDelay()
{
    auto node = a3::IemNode::create (a3::IemKind::dualDelay, 16, 16);
    EXPECT_TRUE (node != nullptr);
    return node;
}

float pitchL (a3::IemNode& node) { return node.rawParameter ("pitchL")->load(); }
}

TEST (DelayModulation, SetsBasePlusDepthTimesShape)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "pitchL", 10.0f, 18.0f, 0.25f, "sine" } });

    modulation.tick (0.0, *node);       // 0.5 + 0.5 sin 0 = 0.5
    EXPECT_NEAR (pitchL (*node), 10.0f + 18.0f * 0.5f, 0.06f);   // the parameter snaps to 0.1

    modulation.tick (1.0, *node);       // quarter period: sine at its peak
    EXPECT_NEAR (pitchL (*node), 10.0f + 18.0f, 0.06f);

    modulation.tick (3.0, *node);       // three quarters: sine at its trough
    EXPECT_NEAR (pitchL (*node), 10.0f, 0.06f);
}

TEST (DelayModulation, TriangleRunsZeroToOneToZero)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "pitchL", 0.0f, 18.0f, 0.5f, "triangle" } });
    modulation.tick (0.0, *node);
    EXPECT_NEAR (pitchL (*node), 0.0f, 0.06f);
    modulation.tick (1.0, *node);       // half a period
    EXPECT_NEAR (pitchL (*node), 18.0f, 0.06f);
    modulation.tick (0.5, *node);
    EXPECT_NEAR (pitchL (*node), 9.0f, 0.06f);
}

TEST (DelayModulation, RateZeroKeepsTheBase)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "pitchL", 7.0f, 18.0f, 0.0f, "random" } });
    for (double t : { 0.0, 1.0, 123.4 })
    {
        modulation.tick (t, *node);
        EXPECT_NEAR (pitchL (*node), 7.0f, 0.06f);
    }
}

TEST (DelayModulation, RandomStaysInTheRangeAndIsDeterministic)
{
    auto first = makeDualDelay();
    auto second = makeDualDelay();
    const std::vector<a3::ModulatedParameter> spec { { "pitchL", 0.0f, 18.0f, 0.125f, "random" } };
    a3::DelayModulation a (spec), b (spec);
    float lowest = 1000.0f, highest = -1000.0f;
    for (int step = 0; step < 4000; ++step)    // 80 s at 50 Hz: ten targets
    {
        a.tick (step * 0.02, *first);
        b.tick (step * 0.02, *second);
        EXPECT_EQ (pitchL (*first), pitchL (*second));
        lowest = std::min (lowest, pitchL (*first));
        highest = std::max (highest, pitchL (*first));
    }
    EXPECT_GE (lowest, 0.0f);
    EXPECT_LE (highest, 18.0f);
    EXPECT_GT (highest - lowest, 3.0f);        // it moves
}

TEST (DelayModulation, RandomInterpolatesLinearlyBetweenTargets)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "pitchL", 0.0f, 18.0f, 0.1f, "random" } });   // a target every 10 s
    modulation.tick (0.0, *node);
    const float start = pitchL (*node);
    modulation.tick (10.0, *node);
    const float end = pitchL (*node);
    modulation.tick (5.0, *node);      // time may be queried at any point inside a segment
    EXPECT_NEAR (pitchL (*node), 0.5f * (start + end), 0.12f);
}

TEST (DelayModulation, DrivesEachParameterWithItsOwnRate)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "pitchL", 0.0f, 18.0f, 0.25f, "sine" },
                                      { "pitchR", 0.0f, 9.0f, 0.0f, "sine" } });
    modulation.tick (1.0, *node);
    EXPECT_NEAR (node->rawParameter ("pitchL")->load(), 18.0f, 0.06f);
    EXPECT_NEAR (node->rawParameter ("pitchR")->load(), 0.0f, 0.06f);   // rate 0: base
}

TEST (DelayModulation, AnUnknownParameterIsIgnoredNotACrash)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "noSuchId", 0.0f, 1.0f, 1.0f, "sine" } });
    modulation.tick (0.3, *node);
    SUCCEED();
}

TEST (DelayModulation, NeverRunsOnTheAudioThread)
{
    // tick() takes the node and a time, never a buffer: there is nothing an audio callback could hand it.
    static_assert (! std::is_invocable_v<decltype (&a3::DelayModulation::tick), a3::DelayModulation&,
                                         juce::AudioBuffer<float>&>);
    SUCCEED();
}

TEST (DelayTempo, WritesTheBpmOfBothSidesAndClampsIt)
{
    auto node = makeDualDelay();
    a3::DelayTempo tempo (*node);
    tempo.set (120.0f);
    EXPECT_FLOAT_EQ (node->rawParameter ("delayBPML")->load(), 120.0f);
    EXPECT_FLOAT_EQ (node->rawParameter ("delayBPMR")->load(), 120.0f);

    tempo.set (std::nanf (""));
    tempo.set (INFINITY);
    EXPECT_FLOAT_EQ (node->rawParameter ("delayBPML")->load(), 120.0f);   // non-finite ignored

    tempo.set (5.0f);                       // below 20: clamped, then held to the plug-in's own 46
    EXPECT_FLOAT_EQ (node->rawParameter ("delayBPML")->load(), 46.0f);
    tempo.set (1000.0f);                    // above 400: clamped, then held to the plug-in's own 320
    EXPECT_FLOAT_EQ (node->rawParameter ("delayBPMR")->load(), 320.0f);
}
