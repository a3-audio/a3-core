// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// XRUNS counts from the moment the engine is up (2026-10-31, the maintainer: the xruns of a start or a reconnect do not
// count). Up: the engine runs and has made its links (when it makes any), the device is there and WirePlumber is not
// restarting. Counting starts at the first round-trip measurement that ends after that, or ten seconds of being up,
// whichever comes first. Down again (the device gone, a WirePlumber restart, another device, a new session) and up
// again is a start again.
#include <gtest/gtest.h>
#include "XrunCount.h"

namespace
{
using State = a3app::RoundTripReport::State;

a3app::BackendStatus status (int xruns, bool linksPlanned = true, State state = State::notMeasured, juce::int64 atMillis = 0)
{
    a3app::BackendStatus s;
    s.xruns = xruns;
    s.sampleRate = 48000;
    s.connecting = true;
    s.linksPlanned = linksPlanned;
    s.devicePresent = true;
    s.roundTripMeasured.state = state;
    s.roundTripMeasured.atMillis = atMillis;
    s.deviceName = "Scarlett 18i20 USB";
    return s;
}
}

TEST (XrunCount, StartUpXrunsDoNotCount)
{
    a3app::XrunCount count;
    EXPECT_EQ (count.since (status (3, false), 0.0), 0) << "no links yet: not up";
    EXPECT_EQ (count.since (status (7, true), 1.0), 0) << "up, the measurement not done yet";
    auto measuring = status (8, true);
    measuring.roundTripMeasured.measuring = true;
    EXPECT_EQ (count.since (measuring, 1.5), 0);
    EXPECT_EQ (count.since (status (9, true, State::measured, 1000), 2.5), 0) << "measured: the count starts here";
    EXPECT_TRUE (count.settled());
    EXPECT_EQ (count.since (status (11, true, State::measured, 1000), 60.0), 2);
}

TEST (XrunCount, WithoutAMeasurementTenSecondsUpSettleIt)
{
    a3app::XrunCount count;
    EXPECT_EQ (count.since (status (2, false), 0.0), 0);
    EXPECT_EQ (count.since (status (4, true), 5.0), 0) << "up from here";
    EXPECT_EQ (count.since (status (5, true), 14.9), 0) << "the ten seconds count from being up, not from the start";
    EXPECT_EQ (count.since (status (6, true), 15.0), 0);
    EXPECT_EQ (count.since (status (8, true), 30.0), 2);
    EXPECT_EQ (count.since (status (8, true, State::noLoop), 31.0), 2);
}

TEST (XrunCount, AMeasurementFromBeforeDoesNotSettleIt)
{
    a3app::XrunCount count;
    EXPECT_EQ (count.since (status (0, true, State::measured, 1000), 0.0), 0) << "measured before it was up";
    EXPECT_EQ (count.since (status (3, true, State::measured, 1000), 5.0), 0);
    EXPECT_EQ (count.since (status (4, true, State::measured, 2000), 6.0), 0) << "a new one: settled at 4";
    EXPECT_EQ (count.since (status (5, true, State::measured, 2000), 7.0), 1);
}

TEST (XrunCount, NoDeviceIsNotUp)
{
    a3app::XrunCount count;
    auto absent = status (0, true);
    absent.devicePresent = false;
    EXPECT_EQ (count.since (absent, 0.0), 0);
    EXPECT_EQ (count.since (absent, 20.0), 0) << "never up without the device";
    EXPECT_FALSE (count.settled());
    EXPECT_EQ (count.since (status (6, true), 21.0), 0) << "the device is back: up from here";
    EXPECT_EQ (count.since (status (7, true), 31.0), 0) << "settled at 7";
    EXPECT_EQ (count.since (status (9, true), 40.0), 2);
}

TEST (XrunCount, AReconnectStartsTheCountOver)
{
    a3app::XrunCount count;
    count.since (status (0, true), 0.0);
    count.since (status (0, true), 10.0);
    EXPECT_EQ (count.since (status (2, true), 12.0), 2);

    auto restarting = status (5, true);
    restarting.timing.restarting = true;
    EXPECT_EQ (count.since (restarting, 13.0), 0) << "WirePlumber restarts: the device goes and comes back";
    EXPECT_EQ (count.since (status (9, true), 14.0), 0);
    EXPECT_EQ (count.since (status (9, true), 24.0), 0) << "ten seconds up again: settled at 9";
    EXPECT_EQ (count.since (status (10, true), 25.0), 1);

    auto gone = status (11, true);
    gone.devicePresent = false;
    EXPECT_EQ (count.since (gone, 26.0), 0) << "the device unplugged";
    EXPECT_EQ (count.since (status (12, true), 27.0), 0);
    EXPECT_EQ (count.since (status (12, true), 37.0), 0);
    EXPECT_EQ (count.since (status (13, true), 38.0), 1);

    auto other = status (14, true);
    other.deviceName = "Device";
    EXPECT_EQ (count.since (other, 39.0), 0) << "another device";
    EXPECT_FALSE (count.settled());
}

TEST (XrunCount, ANewSessionStartsTheCountOver)
{
    a3app::XrunCount count;
    count.since (status (0, true), 0.0);
    count.since (status (0, true), 10.0);
    EXPECT_TRUE (count.settled());
    count.restart();   // Save made a new session: its backend counts from 0
    EXPECT_FALSE (count.settled());
    EXPECT_EQ (count.since (status (3, true), 11.0), 0);
    EXPECT_EQ (count.since (status (4, true), 21.0), 0);
    EXPECT_EQ (count.since (status (6, true), 22.0), 2);
}

TEST (XrunCount, WithoutLinksToMakeRunningIsUp)
{
    a3app::XrunCount count;
    auto noConnect = status (1, false);
    noConnect.connecting = false;   // --no-connect, or a JUCE device
    EXPECT_EQ (count.since (noConnect, 0.0), 0);
    noConnect.xruns = 3;
    EXPECT_EQ (count.since (noConnect, 10.0), 0) << "up from the start: settled at 3";
    noConnect.xruns = 4;
    EXPECT_EQ (count.since (noConnect, 11.0), 1);
    EXPECT_EQ (count.since (a3app::BackendStatus {}, 12.0), 0) << "no engine: not up";
}
