// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Where the delay's tempo comes from (Ruling A4): the beat clock while it runs, else the MASTER
// deck, else the playing deck on the loudest channel, else the last tempo held.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>

#include "TempoSource.h"

using namespace a3core;

namespace
{
std::vector<OscArg> report (double bpm, int playing, int master) { return { pyFloat (bpm), pyInt (playing), pyInt (master) }; }

// Decks on a channel, with that channel's fader; any other deck is on no channel.
TempoSource::FaderOf faders (std::map<int, double> onChannels)
{
    return [onChannels] (int deck) -> std::optional<double> {
        const auto found = onChannels.find (deck);
        if (found == onChannels.end())
            return std::nullopt;
        return found->second;
    };
}

const TempoSource::FaderOf noChannels = faders ({});

// A beat every half second until the source passes the tempo on (a bar at most); `now` moves along.
std::optional<double> beatsAt (TempoSource& source, double bpm, double& now)
{
    for (int i = 0; i < defaultSteadyBeats; ++i)
    {
        const auto answer = source.beat (pyFloat (bpm), now);
        now += 0.5;
        if (answer)
            return answer;
    }
    return std::nullopt;
}
}

//==============================================================================
// The order.

TEST (TempoSource, ALiveBeatClockWinsOverTheMasterDeck)
{
    TempoSource source;
    double now = 10.0;
    source.deckReport (1, report (128.0, 1, 1), now);
    EXPECT_EQ (beatsAt (source, 120.0, now), 120.0);
    EXPECT_FALSE (source.fromDecks (now, noChannels).has_value());
    EXPECT_EQ (source.source(), "beat");
}

TEST (TempoSource, WithoutABeatTheMasterDeckSetsTheTempo)
{
    TempoSource source;
    source.deckReport (2, report (126.0, 1, 1), 10.0);
    EXPECT_EQ (source.fromDecks (10.0, noChannels), 126.0);
    EXPECT_EQ (source.source(), "master deck 2");
}

TEST (TempoSource, TheMasterDeckWinsOverALouderPlayingDeck)
{
    TempoSource source;
    source.deckReport (1, report (130.0, 1, 0), 10.0);
    source.deckReport (2, report (126.0, 1, 1), 10.0);
    EXPECT_EQ (source.fromDecks (10.0, faders ({ { 1, 1.0 }, { 2, 0.2 } })), 126.0);
    EXPECT_EQ (source.source(), "master deck 2");
}

TEST (TempoSource, AMasterDeckThatDoesNotPlayIsPassedOver)
{
    TempoSource source;
    source.deckReport (1, report (130.0, 1, 0), 10.0);
    source.deckReport (2, report (126.0, 0, 1), 10.0);
    EXPECT_EQ (source.fromDecks (10.0, faders ({ { 1, 0.5 }, { 2, 1.0 } })), 130.0);
    EXPECT_EQ (source.source(), "deck 1");
}

TEST (TempoSource, WithoutAMasterThePlayingDeckOnTheHighestFaderWins)
{
    TempoSource source;
    source.deckReport (1, report (130.0, 1, 0), 10.0);
    source.deckReport (2, report (126.0, 1, 0), 10.0);
    source.deckReport (3, report (124.0, 1, 0), 10.0);
    EXPECT_EQ (source.fromDecks (10.0, faders ({ { 1, 0.3 }, { 2, 0.8 }, { 3, 0.5 } })), 126.0);
    EXPECT_EQ (source.source(), "deck 2");
}

TEST (TempoSource, EqualFadersGoToTheLowerDeck)
{
    TempoSource source;
    source.deckReport (3, report (124.0, 1, 0), 10.0);
    source.deckReport (2, report (126.0, 1, 0), 10.0);
    EXPECT_EQ (source.fromDecks (10.0, faders ({ { 2, 0.7 }, { 3, 0.7 } })), 126.0);
    EXPECT_EQ (source.source(), "deck 2");
}

TEST (TempoSource, ADeckOnNoChannelRanksBelowAnyDeckOnAChannel)
{
    TempoSource source;
    source.deckReport (1, report (130.0, 1, 0), 10.0);
    source.deckReport (2, report (126.0, 1, 0), 10.0);
    EXPECT_EQ (source.fromDecks (10.0, faders ({ { 2, 0.0 } })), 126.0);
    EXPECT_EQ (source.source(), "deck 2");
}

TEST (TempoSource, APlayingDeckOnNoChannelStillCountsAlone)
{
    TempoSource source;
    source.deckReport (1, report (130.0, 1, 0), 10.0);
    EXPECT_EQ (source.fromDecks (10.0, noChannels), 130.0);
    EXPECT_EQ (source.source(), "deck 1");
}

TEST (TempoSource, ADeckThatDoesNotPlayOrHasNoTempoIsNotChosen)
{
    TempoSource source;
    source.deckReport (1, report (130.0, 0, 0), 10.0);   // stopped
    source.deckReport (2, report (0.0, 1, 0), 10.0);     // no track loaded
    source.deckReport (3, report (500.0, 1, 1), 10.0);   // no tempo the delay takes
    EXPECT_FALSE (source.fromDecks (10.0, faders ({ { 1, 1.0 }, { 2, 1.0 }, { 3, 1.0 } })).has_value());
    EXPECT_EQ (source.source(), "held");
}

TEST (TempoSource, NothingToFollowHoldsTheLastTempo)
{
    TempoSource source;
    source.deckReport (1, report (128.0, 1, 1), 10.0);
    EXPECT_EQ (source.fromDecks (10.0, noChannels), 128.0);
    source.deckReport (1, report (128.0, 0, 1), 11.0);
    EXPECT_FALSE (source.fromDecks (11.0, noChannels).has_value());
    EXPECT_EQ (source.source(), "held");
}

//==============================================================================
// Freshness.

TEST (TempoSource, TheBeatClockRunsForThreeSecondsAfterItsLastBeat)
{
    TempoSource source;
    double now = 10.0;
    ASSERT_EQ (beatsAt (source, 120.0, now), 120.0);   // the last beat at 11.5
    source.deckReport (1, report (128.0, 1, 1), 13.0);
    EXPECT_FALSE (source.fromDecks (14.5, noChannels).has_value());
    EXPECT_EQ (source.source(), "beat");
    EXPECT_EQ (source.fromDecks (14.51, noChannels), 128.0);   // the next look after it stopped
    EXPECT_EQ (source.source(), "master deck 1");
}

TEST (TempoSource, ABeatWithoutATempoDoesNotKeepTheClockAlive)
{
    TempoSource source;
    double now = 10.0;
    ASSERT_EQ (beatsAt (source, 120.0, now), 120.0);   // the last beat at 11.5
    source.beat (pyFloat (5000.0), 14.0);
    source.deckReport (1, report (128.0, 1, 1), 14.0);
    EXPECT_EQ (source.fromDecks (14.6, noChannels), 128.0);
}

TEST (TempoSource, ADeckReportCountsForFiveSeconds)
{
    TempoSource source;
    source.deckReport (1, report (128.0, 1, 1), 10.0);
    EXPECT_EQ (source.fromDecks (15.0, noChannels), 128.0);
    EXPECT_EQ (source.source(), "master deck 1");
    EXPECT_FALSE (source.fromDecks (15.01, noChannels).has_value());
    EXPECT_EQ (source.source(), "held");
}

//==============================================================================
// The deadband.

TEST (TempoSource, ADeckTempoGoesAtOnceAndThenOnlyPastTheDeadband)
{
    TempoSource source;
    source.deckReport (1, report (128.0, 1, 1), 10.0);
    EXPECT_EQ (source.fromDecks (10.0, noChannels), 128.0);   // no bar to wait for: a deck's tempo is exact
    source.deckReport (1, report (128.05, 1, 1), 10.5);
    EXPECT_FALSE (source.fromDecks (10.5, noChannels).has_value());
    source.deckReport (1, report (128.2, 1, 1), 11.0);
    EXPECT_EQ (source.fromDecks (11.0, noChannels), 128.2);
    EXPECT_FALSE (source.fromDecks (11.5, noChannels).has_value());   // nothing new, nothing sent
}

TEST (TempoSource, ASwitchOfSourceIsMeasuredAgainstWhatWasSent)
{
    TempoSource source;
    double now = 10.0;
    ASSERT_EQ (beatsAt (source, 120.0, now), 120.0);   // the last beat at 11.5
    source.deckReport (1, report (120.05, 1, 1), 15.0);
    EXPECT_FALSE (source.fromDecks (15.0, noChannels).has_value());
    EXPECT_EQ (source.source(), "master deck 1");
    source.deckReport (1, report (121.0, 1, 1), 15.5);
    EXPECT_EQ (source.fromDecks (15.5, noChannels), 121.0);

    now = 16.0;
    EXPECT_FALSE (beatsAt (source, 121.05, now).has_value());   // the beat is back, as close as the deck was
    EXPECT_FALSE (source.beat (pyFloat (121.05), now).has_value());
    EXPECT_EQ (source.source(), "beat");
    now += 0.5;
    EXPECT_EQ (beatsAt (source, 125.0, now), 125.0);
}

//==============================================================================
// What a report has to be.

TEST (TempoSource, AMalformedReportIsRefusedAndChangesNothing)
{
    TempoSource source;
    ASSERT_TRUE (source.deckReport (1, report (128.0, 1, 1), 10.0));
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<std::vector<OscArg>> malformed {
        {},
        { pyFloat (130.0), pyInt (1) },
        { pyFloat (130.0), pyInt (1), pyInt (1), pyInt (1) },
        { pyStr ("130"), pyInt (1), pyInt (1) },
        { pyFloat (nan), pyInt (1), pyInt (1) },
        { pyFloat (-1.0), pyInt (1), pyInt (1) },
        { pyFloat (130.0), pyInt (2), pyInt (1) },
        { pyFloat (130.0), pyInt (1), pyFloat (1.0) },
        { pyFloat (130.0), pyStr ("1"), pyInt (1) },
    };
    for (const auto& args : malformed)
        EXPECT_FALSE (source.deckReport (1, args, 12.0)) << args.size();
    EXPECT_EQ (source.fromDecks (15.0, noChannels), 128.0);   // still the report of 10.0, still MASTER
    EXPECT_FALSE (source.fromDecks (15.01, noChannels).has_value());
}

TEST (TempoSource, ADeckWithNoTrackIsAReportAllTheSame)
{
    TempoSource source;
    EXPECT_TRUE (source.deckReport (1, report (0.0, 0, 0), 10.0));
}
