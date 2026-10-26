// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_tempo.py (B0 f3345dc7feeb): pass the tempo on, but not
// every beat -- a deadband, and a tempo has to hold still for a bar.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <type_traits>

#include "Tempo.h"

using namespace a3core;

namespace
{
// Hand the same tempo in until the follower accepts it (settled () of the Python test).
std::optional<double> settled (TempoFollower& follower, double bpm, int beats = defaultSteadyBeats)
{
    for (int i = 0; i < beats; ++i)
        if (const auto answer = follower.wanted (pyFloat (bpm)))
            return answer;
    return std::nullopt;
}

TempoFollower settledAt (double bpm)
{
    TempoFollower follower;
    settled (follower, bpm);
    return follower;
}

std::optional<double> wanted (TempoFollower& follower, double bpm) { return follower.wanted (pyFloat (bpm)); }
}

// TheFirstOneStillHasToHoldStill

TEST (Tempo, OneBeatIsNotATempoYet)
{
    TempoFollower follower;
    EXPECT_FALSE (wanted (follower, 132.5).has_value());
}

TEST (Tempo, ABarOfTheSameTempoIs)
{
    TempoFollower follower;
    EXPECT_EQ (settled (follower, 132.5), 132.5);
}

TEST (Tempo, AndThenItStaysQuiet)
{
    TempoFollower follower;
    settled (follower, 132.5);
    for (int i = 0; i < 8; ++i)
        EXPECT_FALSE (wanted (follower, 132.5).has_value());
}

// TheDeadband (each starts from a follower settled at 130)

TEST (Tempo, AWobbleSmallerThanTheDeadbandIsNotWorthAMessage)
{
    auto follower = settledAt (130.0);
    for (const double bpm : { 130.0, 130.05, 129.96 })
        EXPECT_FALSE (wanted (follower, bpm).has_value()) << bpm;
}

TEST (Tempo, ARealChangeGoesThroughOnceItHolds)
{
    auto follower = settledAt (130.0);
    EXPECT_FALSE (wanted (follower, 140.0).has_value());
    EXPECT_EQ (settled (follower, 140.0), 140.0);
}

TEST (Tempo, TheDeadbandIsMeasuredFromWhatWasSentNotFromTheLastLook)
{
    auto follower = settledAt (130.0);
    for (const double bpm : { 130.05, 130.09, 130.05, 130.09 })
        EXPECT_FALSE (wanted (follower, bpm).has_value()) << bpm;
    EXPECT_FALSE (wanted (follower, 130.09).has_value());
}

TEST (Tempo, ClearlyOverTheDeadbandGoesAndClearlyUnderStays)
{
    // The exact edge is deliberately not pinned: 130.0 + 0.1 is smaller than a deadband in binary.
    auto follower = settledAt (130.0);
    EXPECT_FALSE (wanted (follower, 130.0 + defaultDeadband / 2).has_value());
    EXPECT_EQ (settled (follower, 130.0 + defaultDeadband * 2), 130.0 + defaultDeadband * 2);
}

// WhatIsNotATempo

TEST (Tempo, ZeroIsRefused)
{
    TempoFollower follower;
    EXPECT_FALSE (wanted (follower, 0.0).has_value());
}

TEST (Tempo, NegativeIsRefused)
{
    TempoFollower follower;
    EXPECT_FALSE (wanted (follower, -120.0).has_value());
}

TEST (Tempo, AbsurdlyFastIsRefused)
{
    TempoFollower follower;
    EXPECT_FALSE (wanted (follower, 100000.0).has_value());
}

TEST (Tempo, NotANumberIsRefused)
{
    TempoFollower follower;
    EXPECT_FALSE (wanted (follower, std::nan ("")).has_value());
    EXPECT_FALSE (wanted (follower, std::numeric_limits<double>::infinity()).has_value());
}

TEST (Tempo, ARefusalDoesNotDisturbWhatWasSent)
{
    TempoFollower follower;
    EXPECT_EQ (settled (follower, 130.0), 130.0);
    EXPECT_FALSE (wanted (follower, 0.0).has_value());
    EXPECT_FALSE (wanted (follower, 130.0).has_value());
}

// TheRangeItAccepts

TEST (Tempo, TheEndsAreIn)
{
    TempoFollower low, high;
    EXPECT_EQ (settled (low, bpmMin), bpmMin);
    EXPECT_EQ (settled (high, bpmMax), bpmMax);
}

TEST (Tempo, JustOutsideIsOut)
{
    TempoFollower low, high;
    EXPECT_FALSE (settled (low, bpmMin - 0.01).has_value());
    EXPECT_FALSE (settled (high, bpmMax + 0.01).has_value());
}

// NoChangeIsNotAZero

TEST (Tempo, ItRefusesToBeReadAsANumber)
{
    // Python's NO_CHANGE raises when truth-tested so that "nothing to send" cannot collapse into a
    // tempo of nothing. Here the answer is an optional: nothing to send is an empty one, and a
    // tempo of 0.0 would still be an engaged one -- the two cannot be confused.
    static_assert (std::is_same_v<decltype (TempoFollower().wanted (pyFloat (0.0))), std::optional<double>>);
    EXPECT_FALSE (TempoFollower().wanted (pyFloat (0.0)).has_value());
    EXPECT_TRUE (std::optional<double> (0.0).has_value());
}

// ATempoOnTheMoveIsNotATempoYet

TEST (Tempo, ARampSendsNothing)
{
    auto follower = settledAt (130.0);
    for (const double bpm : { 129.0, 128.0, 127.0, 126.0, 125.0, 124.0, 123.0 })
        EXPECT_FALSE (wanted (follower, bpm).has_value()) << bpm;
}

TEST (Tempo, AndTheTempoItComesToRestOnDoes)
{
    auto follower = settledAt (130.0);
    for (const double bpm : { 129.0, 128.0, 127.0, 126.0 })
        wanted (follower, bpm);
    EXPECT_EQ (settled (follower, 126.0), 126.0);
}

TEST (Tempo, WanderingBackAndForthNeverSettles)
{
    auto follower = settledAt (130.0);
    for (int i = 0; i < 20; ++i)
    {
        EXPECT_FALSE (wanted (follower, 124.0).has_value());
        EXPECT_FALSE (wanted (follower, 126.0).has_value());
    }
}

TEST (Tempo, ARunIsCountedInBeatsNotInSeconds)
{
    EXPECT_EQ (defaultSteadyBeats, 4);
}
