// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_presence.py (B0 f3345dc7feeb): whether a device is still
// there, from its hello every 30 s.
#include <gtest/gtest.h>

#include "Presence.h"

using namespace a3core;
using Strings = std::vector<std::string>;

TEST (Presence, TheFirstHelloIsNews)
{
    Presence p (60);
    EXPECT_TRUE (p.heard (0.0));
}

TEST (Presence, TheNextHelloInTimeIsNot)
{
    Presence p (60);
    p.heard (0.0);
    EXPECT_FALSE (p.heard (30.0));
    EXPECT_TRUE (p.present (80.0));
}

TEST (Presence, SilenceIsNoticedOnce)
{
    Presence p (60);
    p.heard (0.0);
    EXPECT_FALSE (p.gone (59.0));
    EXPECT_TRUE (p.gone (61.0));
    EXPECT_FALSE (p.gone (90.0));
    EXPECT_FALSE (p.present (61.0));
}

TEST (Presence, AHelloAfterSilenceIsNewsAgain)
{
    Presence p (60);
    p.heard (0.0);
    p.gone (61.0);
    EXPECT_TRUE (p.heard (70.0));
}

TEST (Presence, NeverHeardIsNotPresentAndNeverGone)
{
    Presence p (60);
    EXPECT_FALSE (p.present (0.0));
    EXPECT_FALSE (p.gone (1000.0));
}

// WhatAHelloMeansForTheLink

TEST (Presence, TheFirstHelloConnects)
{
    HelloWatch w (60);
    EXPECT_TRUE (w.hello ("10.0.0.5", 0.0));
    EXPECT_EQ (w.host(), "10.0.0.5");
}

TEST (Presence, AHelloInTimeFromTheSameHostDoesNothing)
{
    HelloWatch w (60);
    w.hello ("10.0.0.5", 0.0);
    EXPECT_FALSE (w.hello ("10.0.0.5", 30.0));
}

TEST (Presence, AHelloFromAnotherHostReconnects)
{
    HelloWatch w (60);
    w.hello ("10.0.0.5", 0.0);
    EXPECT_TRUE (w.hello ("10.0.0.6", 30.0));
    EXPECT_EQ (w.host(), "10.0.0.6");
}

TEST (Presence, SilenceIsNoticedOnceAndForgetsTheHost)
{
    HelloWatch w (60);
    w.hello ("10.0.0.5", 0.0);
    EXPECT_FALSE (w.silence (30.0));
    EXPECT_TRUE (w.silence (61.0));
    EXPECT_FALSE (w.silence (91.0));
    EXPECT_FALSE (w.host().has_value());
}

TEST (Presence, AHelloAfterSilenceConnectsAgain)
{
    HelloWatch w (60);
    w.hello ("10.0.0.5", 0.0);
    w.silence (61.0);
    EXPECT_TRUE (w.hello ("10.0.0.5", 90.0));
}

TEST (Presence, AHelloIsNeverSilenceAtTheSameMoment)
{
    HelloWatch w (60);
    w.hello ("10.0.0.5", 0.0);
    w.silence (61.0);
    w.hello ("10.0.0.5", 90.0);
    EXPECT_FALSE (w.silence (90.0));
}

TEST (Presence, NoStemdeckEverIsNeverSilence)
{
    EXPECT_FALSE (HelloWatch (60).silence (1000.0));
}

// TwoHostsSayingHelloInTurn

TEST (Presence, TheOneThatArrivedFirstDoesNotTakeItBack)
{
    HelloWatch w (60);
    w.hello ("10.0.0.5", 0.0);
    EXPECT_TRUE (w.hello ("10.0.0.6", 10.0));
    EXPECT_FALSE (w.hello ("10.0.0.5", 30.0));
    EXPECT_EQ (w.host(), "10.0.0.6");
}

TEST (Presence, OneThatWasSilentArrivesAgain)
{
    HelloWatch w (60);
    w.hello ("10.0.0.5", 0.0);
    w.hello ("10.0.0.6", 10.0);
    w.hello ("10.0.0.6", 70.0);
    EXPECT_TRUE (w.hello ("10.0.0.5", 80.0));
    EXPECT_EQ (w.host(), "10.0.0.5");
}

TEST (Presence, TheSilenceOfTheOneFollowedIsNoticed)
{
    HelloWatch w (60);
    w.hello ("10.0.0.5", 0.0);
    w.hello ("10.0.0.6", 10.0);
    w.hello ("10.0.0.5", 60.0);
    EXPECT_TRUE (w.silence (71.0));
    EXPECT_FALSE (w.host().has_value());
}

TEST (Presence, WithNobodyFollowedAnyHelloConnects)
{
    HelloWatch w (60);
    w.hello ("10.0.0.5", 0.0);
    w.hello ("10.0.0.6", 10.0);
    w.hello ("10.0.0.5", 60.0);
    w.silence (71.0);
    EXPECT_TRUE (w.hello ("10.0.0.5", 90.0));
    EXPECT_EQ (w.host(), "10.0.0.5");
}

// ARemoteKeepsTheLead

namespace
{
const std::string rig = "127.0.0.1";

HelloWatch remoteAwareWatch()
{
    return HelloWatch (60, [] (const std::string& host) { return host == rig; });
}
}

TEST (Presence, TheRigArrivingDoesNotTakeItFromARemoteOne)
{
    auto w = remoteAwareWatch();
    w.hello ("10.0.0.20", 0.0);
    EXPECT_FALSE (w.hello (rig, 10.0));
    EXPECT_EQ (w.host(), "10.0.0.20");
}

TEST (Presence, AfterACoreStartTheRemoteOneTakesItFromTheRig)
{
    // Both arrive anew at Core's start; the rig's own may say hello first.
    auto w = remoteAwareWatch();
    w.hello (rig, 0.0);
    EXPECT_TRUE (w.hello ("10.0.0.20", 5.0));
    EXPECT_EQ (w.host(), "10.0.0.20");
    EXPECT_FALSE (w.hello (rig, 30.0));
}

TEST (Presence, TheRigTakesItBackOnceTheRemoteOneIsQuiet)
{
    auto w = remoteAwareWatch();
    w.hello ("10.0.0.20", 0.0);
    w.hello (rig, 10.0);
    EXPECT_TRUE (w.silence (61.0));
    EXPECT_TRUE (w.hello (rig, 70.0));
    EXPECT_EQ (w.host(), rig);
}

TEST (Presence, AmongRemoteOnesTheNewestArrivalStillWins)
{
    auto w = remoteAwareWatch();
    w.hello ("10.0.0.20", 0.0);
    EXPECT_TRUE (w.hello ("10.0.0.21", 10.0));
    EXPECT_FALSE (w.hello ("10.0.0.20", 30.0));
    EXPECT_EQ (w.host(), "10.0.0.21");
}

// OnlyOneMayRun

namespace
{
HelloWatch oneAtATimeWatch()
{
    HelloWatch watch (60, {}, true);
    watch.hello ("a", 0.0);
    return watch;
}
}

TEST (Presence, ANewcomerDoesNotTakeTheLinkWhileTheFollowedIsAlive)
{
    auto watch = oneAtATimeWatch();
    EXPECT_FALSE (watch.hello ("b", 10.0));
    EXPECT_EQ (watch.host(), "a");
}

TEST (Presence, TheNewcomerIsReportedOnce)
{
    auto watch = oneAtATimeWatch();
    watch.hello ("b", 10.0);
    EXPECT_EQ (watch.takeRivals(), Strings { "b" });
    watch.hello ("b", 40.0);
    EXPECT_EQ (watch.takeRivals(), Strings {});
}

TEST (Presence, ANewcomerThatWentQuietAndReturnsIsReportedAgain)
{
    auto watch = oneAtATimeWatch();
    watch.hello ("b", 10.0);
    watch.takeRivals();
    watch.hello ("a", 60.0);
    watch.hello ("a", 90.0);   // b is forgotten: silent
    watch.hello ("b", 100.0);
    EXPECT_EQ (watch.takeRivals(), Strings { "b" });
}

TEST (Presence, TheFollowedOneSayingHelloAgainIsNoRival)
{
    auto watch = oneAtATimeWatch();
    watch.hello ("a", 30.0);
    EXPECT_EQ (watch.takeRivals(), Strings {});
}

TEST (Presence, OnceTheFollowedOneIsSilentTheNewcomerIsFollowed)
{
    auto watch = oneAtATimeWatch();
    watch.hello ("b", 10.0);
    watch.hello ("b", 40.0);
    watch.hello ("b", 70.0);
    EXPECT_TRUE (watch.silence (70.0));
    EXPECT_TRUE (watch.hello ("b", 100.0));
    EXPECT_EQ (watch.host(), "b");
}

TEST (Presence, TheDefaultStillLetsTheNewestArrivalWin)
{
    HelloWatch watch (60);
    watch.hello ("a", 0.0);
    EXPECT_TRUE (watch.hello ("b", 10.0));
    EXPECT_EQ (watch.takeRivals(), Strings {});
}
