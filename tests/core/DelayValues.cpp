// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The four delay values as integer steps: exact values, clamped ends, the COLOR snap (R11), and the
// beat that plays against the one selected (R16).
#include <gtest/gtest.h>

#include <cmath>

#include "DelayValues.h"

using namespace a3core;

TEST (DelayValues, TheDefaultsAreTheDocumentedOnes)
{
    const DelaySteps steps;
    EXPECT_EQ (delayValue (steps, DelayWord::beat), 0.25);
    EXPECT_EQ (delayValue (steps, DelayWord::feedback), 0.7);
    EXPECT_EQ (delayValue (steps, DelayWord::color), 0.0);
    EXPECT_EQ (delayValue (steps, DelayWord::spread), 60.0);
}

TEST (DelayValues, BeatStepsThroughTheLegalBeatsAndStopsAtBothEnds)
{
    DelaySteps steps;
    std::vector<double> seen { delayValue (steps, DelayWord::beat) };
    for (int i = 0; i < 8; ++i)
    {
        turnDelay (steps, DelayWord::beat, 1);
        seen.push_back (delayValue (steps, DelayWord::beat));
    }
    EXPECT_EQ (seen, (std::vector<double> { 0.25, 0.5, 0.75, 1, 2, 2, 2, 2, 2 }));   // no 4, no wrap
    turnDelay (steps, DelayWord::beat, -100);
    EXPECT_EQ (delayValue (steps, DelayWord::beat), 0.125);
    turnDelay (steps, DelayWord::beat, -1);
    EXPECT_EQ (delayValue (steps, DelayWord::beat), 0.125);
}

TEST (DelayValues, ADetentCountMovesThatManySteps)
{
    DelaySteps steps;
    turnDelay (steps, DelayWord::spread, 3);
    EXPECT_EQ (delayValue (steps, DelayWord::spread), 105.0);
    turnDelay (steps, DelayWord::spread, -2);
    EXPECT_EQ (delayValue (steps, DelayWord::spread), 75.0);
    turnDelay (steps, DelayWord::spread, 99);
    EXPECT_EQ (delayValue (steps, DelayWord::spread), 180.0);
    turnDelay (steps, DelayWord::spread, -999);
    EXPECT_EQ (delayValue (steps, DelayWord::spread), 0.0);
}

TEST (DelayValues, FeedbackRunsFromZeroToNinetyFivePercent)
{
    DelaySteps steps;
    turnDelay (steps, DelayWord::feedback, 99);
    EXPECT_EQ (delayValue (steps, DelayWord::feedback), 0.95);
    turnDelay (steps, DelayWord::feedback, -99);
    EXPECT_EQ (delayValue (steps, DelayWord::feedback), 0.0);
}

TEST (DelayValues, ColorStepsAreExactTenths)
{
    DelaySteps steps;
    turnDelay (steps, DelayWord::color, 3);
    EXPECT_EQ (delayValue (steps, DelayWord::color), 3 / 10.0);   // not 0.1 + 0.1 + 0.1
    turnDelay (steps, DelayWord::color, 99);
    EXPECT_EQ (delayValue (steps, DelayWord::color), 1.0);
}

TEST (DelayValues, ATurnThroughTheColorCentreStopsOnIt)
{
    DelaySteps steps;
    turnDelay (steps, DelayWord::color, 2);
    turnDelay (steps, DelayWord::color, -5);
    EXPECT_EQ (steps.color, 0);
    turnDelay (steps, DelayWord::color, -1);
    EXPECT_EQ (delayValue (steps, DelayWord::color), -0.1);
    turnDelay (steps, DelayWord::color, 4);
    EXPECT_EQ (steps.color, 0);
    turnDelay (steps, DelayWord::color, 4);   // from the centre a turn goes on
    EXPECT_EQ (delayValue (steps, DelayWord::color), 0.4);
}

TEST (DelayValues, AStoredValueLandsOnTheNearestLegalStep)
{
    DelaySteps steps;
    setDelayFromValue (steps, DelayWord::beat, 0.7);
    EXPECT_EQ (delayValue (steps, DelayWord::beat), 0.75);
    setDelayFromValue (steps, DelayWord::beat, 4.0);
    EXPECT_EQ (delayValue (steps, DelayWord::beat), 2.0);
    setDelayFromValue (steps, DelayWord::feedback, 0.33);
    EXPECT_EQ (delayValue (steps, DelayWord::feedback), 0.35);
    setDelayFromValue (steps, DelayWord::feedback, 5.0);
    EXPECT_EQ (delayValue (steps, DelayWord::feedback), 0.95);
    setDelayFromValue (steps, DelayWord::color, -3.0);
    EXPECT_EQ (delayValue (steps, DelayWord::color), -1.0);
    setDelayFromValue (steps, DelayWord::spread, 100.0);
    EXPECT_EQ (delayValue (steps, DelayWord::spread), 105.0);
}

TEST (DelayValues, ValueThatIsNoNumberLeavesTheStep)
{
    DelaySteps steps;
    turnDelay (steps, DelayWord::spread, 2);
    setDelayFromValue (steps, DelayWord::spread, std::nan (""));
    EXPECT_EQ (delayValue (steps, DelayWord::spread), 90.0);
}

TEST (DelayValues, TheBeatThatPlaysIsTheSelectedOneUnlessTheLineIsTooShort)
{
    DelaySteps steps;
    setDelayFromValue (steps, DelayWord::beat, 2.0);
    EXPECT_EQ (announcedBeat (steps, std::nullopt), 2.0);   // no tempo known: the selection
    EXPECT_EQ (announcedBeat (steps, 120.0), 2.0);
    EXPECT_EQ (announcedBeat (steps, 92.0), 2.0);
    EXPECT_EQ (announcedBeat (steps, 90.0), 1.0);
    EXPECT_EQ (delayValue (steps, DelayWord::beat), 2.0);   // the selection itself is kept
    setDelayFromValue (steps, DelayWord::beat, 0.25);
    EXPECT_EQ (announcedBeat (steps, 60.0), 0.25);
}

TEST (DelayValues, TheTurnWordsAreTheTruthsKeys)
{
    EXPECT_EQ (delayWordOfTurnKey ("aux-return.delay.color.turn"), DelayWord::color);
    EXPECT_EQ (delayWordOfTurnKey ("aux-return.delay.color"), std::nullopt);
    EXPECT_EQ (delayWordOfTurnKey ("aux-return.stem.push"), std::nullopt);
}
