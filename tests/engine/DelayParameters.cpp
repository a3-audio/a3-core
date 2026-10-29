// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include "DelayParameters.h"

namespace delay = a3::delay;

namespace
{
double secondsOf (const delay::DelayTime& t) { return 60.0 / ((double) t.bpm * t.mult); }
double octaves (float from, float to) { return std::log2 ((double) to / (double) from); }
}

TEST (DelayParameters, TheDefaultsAreTheRulings)
{
    EXPECT_FLOAT_EQ (delay::defaultBeat, 0.25f);       // R1: today's sound, a quarter of a beat
    EXPECT_FLOAT_EQ (delay::defaultFeedback, 0.70f);   // R10
    EXPECT_FLOAT_EQ (delay::defaultColor, 0.0f);
    EXPECT_FLOAT_EQ (delay::defaultSpread, 60.0f);     // R9
}

TEST (DelayParameters, ABeatSnapsToTheNearestLegalOneInLog)
{
    for (float legal : delay::legalBeats)
        EXPECT_FLOAT_EQ (delay::snapBeat (legal), legal);
    EXPECT_FLOAT_EQ (delay::snapBeat (0.3f), 0.25f);
    EXPECT_FLOAT_EQ (delay::snapBeat (0.6f), 0.5f);    // the log midpoint of 0.5 and 0.75 is 0.612
    EXPECT_FLOAT_EQ (delay::snapBeat (0.62f), 0.75f);
    EXPECT_FLOAT_EQ (delay::snapBeat (2.7f), 2.0f);    // ... of 2 and 4 is 2.83
    EXPECT_FLOAT_EQ (delay::snapBeat (3.0f), 4.0f);
    EXPECT_FLOAT_EQ (delay::snapBeat (100.0f), 4.0f);
    EXPECT_FLOAT_EQ (delay::snapBeat (0.01f), 0.125f);
    EXPECT_FLOAT_EQ (delay::snapBeat (0.0f), 0.125f);
    EXPECT_FLOAT_EQ (delay::snapBeat (-1.0f), 0.125f);
}

TEST (DelayParameters, TheOtherValuesAreClamped)
{
    EXPECT_FLOAT_EQ (delay::clampFeedback (2.0f), 0.95f);
    EXPECT_FLOAT_EQ (delay::clampFeedback (-1.0f), 0.0f);
    EXPECT_FLOAT_EQ (delay::clampFeedback (0.42f), 0.42f);
    EXPECT_FLOAT_EQ (delay::clampColor (3.0f), 1.0f);
    EXPECT_FLOAT_EQ (delay::clampColor (-3.0f), -1.0f);
    EXPECT_FLOAT_EQ (delay::clampColor (-0.3f), -0.3f);
    EXPECT_FLOAT_EQ (delay::clampSpread (270.0f), 180.0f);
    EXPECT_FLOAT_EQ (delay::clampSpread (-10.0f), 0.0f);
    EXPECT_FLOAT_EQ (delay::clampSpread (45.0f), 45.0f);
}

TEST (DelayParameters, EveryLegalBeatAt120PlaysItsTimeExceptFourWhichHalves)
{
    for (float beats : delay::legalBeats)
    {
        const auto t = delay::delayTimeFor (120.0f, beats);
        const float played = delay::fitBeatsToLine (120.0f, beats);
        EXPECT_FLOAT_EQ (t.beats, played) << beats;
        EXPECT_NEAR (secondsOf (t), played * 60.0 / 120.0, 1e-6) << beats;
        EXPECT_GE (t.bpm, delay::lowestDelayBpm) << beats;
        EXPECT_LE (t.bpm, 320.0f) << beats;
        EXPECT_GE (t.mult, 1) << beats;
        EXPECT_LE (t.mult, 16) << beats;
    }
}

TEST (DelayParameters, TheMultiplierIsTheSmallestThatFits)
{
    const auto four200 = delay::delayTimeFor (200.0f, 4.0f);   // 1.2 s fits the line
    EXPECT_FLOAT_EQ (four200.beats, 4.0f);
    EXPECT_EQ (four200.mult, 1);
    EXPECT_FLOAT_EQ (four200.bpm, 50.0f);

    const auto threeQuarters = delay::delayTimeFor (129.0f, 0.75f);
    EXPECT_EQ (threeQuarters.mult, 1);
    EXPECT_FLOAT_EQ (threeQuarters.bpm, 172.0f);

    const auto eighth = delay::delayTimeFor (320.0f, 0.125f);   // 2560 BPM of echoes: 320 x 8
    EXPECT_EQ (eighth.mult, 8);
    EXPECT_FLOAT_EQ (eighth.bpm, 320.0f);

    const auto quarter = delay::delayTimeFor (120.0f, 0.25f);   // the default: 480 = 240 x 2
    EXPECT_EQ (quarter.mult, 2);
    EXPECT_FLOAT_EQ (quarter.bpm, 240.0f);
}

// R2 (pending): every time fits the line; a time beyond it falls back by fitBeatsToLine, which
// keeps a beat that fits and returns one that fits.
TEST (DelayParameters, EveryBeatFitsTheLine)
{
    for (const float tempo : { 20.0f, 60.0f, 91.0f, 92.0f, 120.0f, 129.0f, 183.0f, 184.0f, 200.0f, 400.0f })
        for (const float beats : delay::legalBeats)
        {
            const auto t = delay::delayTimeFor (tempo, beats);
            const float fitted = delay::fitBeatsToLine (tempo, beats);
            EXPECT_FLOAT_EQ (t.beats, fitted) << tempo << " " << beats;
            EXPECT_LE (60.0 * fitted / tempo, delay::longestDelaySeconds * (1.0 + 1e-6)) << tempo << " " << beats;
            if (60.0 * beats / tempo <= delay::longestDelaySeconds)
                EXPECT_FLOAT_EQ (fitted, beats) << "fits already: " << tempo << " " << beats;
            EXPECT_NEAR (secondsOf (t), 60.0 * fitted / tempo, 1e-6) << tempo << " " << beats;
        }
    EXPECT_NEAR (delay::longestDelaySeconds, 1.304, 1e-3);
}

// The policy itself (R2, pending): the one test that changes with it.
TEST (DelayParameters, TheFallbackHalvesTheBeats)
{
    EXPECT_FLOAT_EQ (delay::fitBeatsToLine (120.0f, 4.0f), 2.0f);
    EXPECT_FLOAT_EQ (delay::fitBeatsToLine (60.0f, 4.0f), 1.0f);
    EXPECT_FLOAT_EQ (delay::fitBeatsToLine (91.0f, 2.0f), 1.0f);
    EXPECT_FLOAT_EQ (delay::fitBeatsToLine (184.0f, 4.0f), 4.0f);   // the floor exactly: fits
    EXPECT_FLOAT_EQ (delay::fitBeatsToLine (20.0f, 1.0f), 0.25f);
}

TEST (DelayParameters, FeedbackSplitsTheLoopGainInTodaysRatio)
{
    const auto g = delay::feedbackGains (0.7f);
    EXPECT_NEAR (g.direct, 0.7f * 0.738f, 1e-6f);
    EXPECT_NEAR (g.cross, 0.7f * 0.262f, 1e-6f);
    EXPECT_NEAR (g.direct + g.cross, 0.7f, 1e-6f);
    const auto off = delay::feedbackGains (0.0f);
    EXPECT_EQ (off.direct, 0.0f);
    EXPECT_EQ (off.cross, 0.0f);
}

TEST (DelayParameters, FeedbackDecibelsAreFlooredAtMinus60)
{
    EXPECT_NEAR (delay::feedbackDb (0.501187f), -6.0f, 1e-3f);
    EXPECT_FLOAT_EQ (delay::feedbackDb (1.0f), 0.0f);
    EXPECT_FLOAT_EQ (delay::feedbackDb (0.0f), -60.0f);
    EXPECT_FLOAT_EQ (delay::feedbackDb (1e-5f), -60.0f);
}

TEST (DelayParameters, ColorMovesOneFilterAndLeavesTheOther)
{
    const auto centre = delay::cutoffsFor (0.0f);
    EXPECT_FLOAT_EQ (centre.highPass, 233.0f);
    EXPECT_FLOAT_EQ (centre.lowPass, 8000.0f);

    const auto highest = delay::cutoffsFor (-1.0f);
    EXPECT_NEAR (highest.highPass, 2500.0f, 0.01f);
    EXPECT_FLOAT_EQ (highest.lowPass, 8000.0f);
    const auto halfHigh = delay::cutoffsFor (-0.5f);
    EXPECT_NEAR (halfHigh.highPass, std::sqrt (233.0f * 2500.0f), 0.01f);

    const auto lowest = delay::cutoffsFor (1.0f);
    EXPECT_FLOAT_EQ (lowest.highPass, 233.0f);
    EXPECT_NEAR (lowest.lowPass, 500.0f, 0.01f);
    const auto halfLow = delay::cutoffsFor (0.5f);
    EXPECT_NEAR (halfLow.lowPass, 2000.0f, 0.01f);
}

TEST (DelayParameters, CutoffsGlideAtMostAThirdOfAnOctavePerStep)
{
    delay::Cutoffs current = delay::cutoffsFor (0.0f);
    const auto target = delay::cutoffsFor (-1.0f);
    int steps = 0;
    while ((current.highPass != target.highPass || current.lowPass != target.lowPass) && steps < 100)
    {
        const auto next = delay::glideCutoffs (current, target);
        EXPECT_LE (std::abs (octaves (current.highPass, next.highPass)), 1.0 / 3.0 + 1e-6);
        EXPECT_LE (std::abs (octaves (current.lowPass, next.lowPass)), 1.0 / 3.0 + 1e-6);
        current = next;
        ++steps;
    }
    EXPECT_EQ (steps, 11);   // 3.42 octaves: 10 full thirds and the rest
    EXPECT_FLOAT_EQ (current.highPass, target.highPass);

    const auto near = delay::glideCutoffs ({ 233.0f, 8000.0f }, { 240.0f, 7900.0f });   // within a third: arrives
    EXPECT_FLOAT_EQ (near.highPass, 240.0f);
    EXPECT_FLOAT_EQ (near.lowPass, 7900.0f);
}

TEST (DelayParameters, SpreadGlidesAtMostTenDegreesPerStep)
{
    EXPECT_FLOAT_EQ (delay::glideSpread (60.0f, 180.0f), 70.0f);
    EXPECT_FLOAT_EQ (delay::glideSpread (60.0f, 0.0f), 50.0f);
    EXPECT_FLOAT_EQ (delay::glideSpread (60.0f, 65.0f), 65.0f);
    EXPECT_FLOAT_EQ (delay::glideSpread (60.0f, 60.0f), 60.0f);
}
