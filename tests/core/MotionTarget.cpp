// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_motion_target.py (B0 f3345dc7feeb): which Motion Core
// talks to -- the rig's own, or the one followed.
#include <gtest/gtest.h>

#include "MotionTarget.h"

using namespace a3core;
using Strings = std::vector<std::string>;

namespace
{
const Endpoint defaultTarget { "127.0.0.1", 7771 };
const std::set<std::string> own { "192.168.8.10" };
const std::string a3nuc2 = "192.168.8.20";
const std::string notebook = "192.168.43.70";

MotionTarget target() { return MotionTarget (defaultTarget, 7771, own, 7772, 60); }
}

// ThisMachine

TEST (MotionTarget, LoopbackIsThisMachine)
{
    EXPECT_TRUE (isThisMachine ("127.0.0.1", own));
    EXPECT_TRUE (isThisMachine ("127.0.1.1", own));
}

TEST (MotionTarget, CoresOwnAddressIsThisMachine)
{
    EXPECT_TRUE (isThisMachine ("192.168.8.10", own));
}

TEST (MotionTarget, AnotherMachineIsNot)
{
    EXPECT_FALSE (isThisMachine (a3nuc2, own));
}

TEST (MotionTarget, ANameIsComparedAsWritten)
{
    EXPECT_FALSE (isThisMachine ("a3nuc2", own));
}

// TheRigsOwnMotion

TEST (MotionTarget, ItIsTheTargetBeforeAnybodySpeaks)
{
    auto t = target();
    EXPECT_EQ (t.current(), defaultTarget);
    EXPECT_FALSE (t.remote());
}

TEST (MotionTarget, ItsHelloChangesNothing)
{
    auto t = target();
    EXPECT_FALSE (t.hello ("127.0.0.1", 0.0).has_value());
    EXPECT_FALSE (t.hello ("192.168.8.10", 30.0).has_value());
    EXPECT_EQ (t.current(), defaultTarget);
}

TEST (MotionTarget, ItsSilenceChangesNothing)
{
    auto t = target();
    t.hello ("127.0.0.1", 0.0);
    EXPECT_FALSE (t.silence (61.0).has_value());
    EXPECT_EQ (t.current(), defaultTarget);
}

// ARemoteMotion

TEST (MotionTarget, ItsHelloPointsCoreAtIt)
{
    auto t = target();
    EXPECT_EQ (t.hello (a3nuc2, 0.0), (Endpoint { a3nuc2, 7771 }));
    EXPECT_EQ (t.current(), (Endpoint { a3nuc2, 7771 }));
    EXPECT_TRUE (t.remote());
}

TEST (MotionTarget, ItsNextHelloIsNoSwitch)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    EXPECT_FALSE (t.hello (a3nuc2, 30.0).has_value());
}

TEST (MotionTarget, ThePortIsTheTruthsNotTheDefault)
{
    MotionTarget t ({ "127.0.0.1", 17771 }, 7771, own, 7772, 60);
    EXPECT_EQ (t.hello (a3nuc2, 0.0), (Endpoint { a3nuc2, 7771 }));
}

TEST (MotionTarget, AMinuteOfSilenceFallsBackToTheRigs)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    EXPECT_FALSE (t.silence (59.0).has_value());
    EXPECT_EQ (t.silence (61.0), defaultTarget);
    EXPECT_FALSE (t.remote());
    EXPECT_FALSE (t.silence (90.0).has_value());
}

TEST (MotionTarget, ARemoteBesideTheRigsRunningMotionDoesNotTakeIt)
{
    // One Motion at a time (2026-10-14): the remote that arrives beside the rig's own is a rival.
    auto t = target();
    t.hello ("127.0.0.1", 0.0);
    EXPECT_FALSE (t.hello (a3nuc2, 10.0).has_value());
    EXPECT_EQ (t.current(), defaultTarget);
    EXPECT_FALSE (t.remote());
}

TEST (MotionTarget, TheRigsMotionStartedAgainDoesNotTakeOver)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    EXPECT_FALSE (t.hello ("127.0.0.1", 10.0).has_value());
    EXPECT_TRUE (t.remote());
}

TEST (MotionTarget, TheRigsMotionTakesOverOnceTheRemoteIsQuiet)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    t.hello ("127.0.0.1", 10.0);
    EXPECT_EQ (t.silence (61.0), defaultTarget);
    EXPECT_FALSE (t.remote());
}

TEST (MotionTarget, ASecondRemoteDoesNotTakeTheLead)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    EXPECT_FALSE (t.hello (notebook, 10.0).has_value());
    EXPECT_EQ (t.current(), (Endpoint { a3nuc2, 7771 }));
    EXPECT_EQ (t.vuDestination(), (Endpoint { a3nuc2, 7772 }));
}

TEST (MotionTarget, TheSecondRemoteTakesOverWhenTheFirstIsQuiet)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    t.hello (notebook, 10.0);
    t.hello (notebook, 40.0);
    EXPECT_EQ (t.silence (65.0), defaultTarget);
    EXPECT_EQ (t.hello (notebook, 70.0), (Endpoint { notebook, 7771 }));
}

TEST (MotionTarget, AfterTheFallbackTheRemoteCanArriveAgain)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    t.silence (61.0);
    EXPECT_EQ (t.hello (a3nuc2, 70.0), (Endpoint { a3nuc2, 7771 }));
}

// TheMetersForARemoteMotion

TEST (MotionTarget, TheRigsOwnGetsNothingFromCore)
{
    auto t = target();
    EXPECT_FALSE (t.vuDestination().has_value());
    t.hello ("127.0.0.1", 0.0);
    EXPECT_FALSE (t.vuDestination().has_value());
}

TEST (MotionTarget, ARemoteOneGetsThemOnItsVuPort)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    EXPECT_EQ (t.vuDestination(), (Endpoint { a3nuc2, 7772 }));
}

TEST (MotionTarget, AfterTheFallbackNothingIsForwarded)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    t.silence (61.0);
    EXPECT_FALSE (t.vuDestination().has_value());
}

// TheBeatForARemoteMotion

TEST (MotionTarget, TheRigsOwnGetsNoBeatFromCore)
{
    auto t = target();
    EXPECT_FALSE (t.forwardsBeat());
    t.hello ("127.0.0.1", 0.0);
    EXPECT_FALSE (t.forwardsBeat());
}

TEST (MotionTarget, ARemoteOneGetsTheBeat)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    EXPECT_TRUE (t.forwardsBeat());
}

TEST (MotionTarget, AfterTheFallbackTheBeatStops)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    t.silence (61.0);
    EXPECT_FALSE (t.forwardsBeat());
}

// TwoMotions

TEST (MotionTarget, ARivalIsReportedOnceWithTheOneFollowed)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    t.hello (notebook, 10.0);
    EXPECT_EQ (t.takeRivals(), Strings { notebook });
    t.hello (notebook, 40.0);
    EXPECT_EQ (t.takeRivals(), Strings {});
    EXPECT_EQ (t.followed(), a3nuc2);
}

TEST (MotionTarget, TheRigsOwnIsNamedByTheAddressItSentFrom)
{
    auto t = target();
    t.hello ("127.0.0.1", 0.0);
    t.hello (a3nuc2, 10.0);
    EXPECT_EQ (t.followed(), "127.0.0.1");
    EXPECT_EQ (t.takeRivals(), Strings { a3nuc2 });
}

TEST (MotionTarget, TheRigBesideARemoteIsARivalNamedByItsAddress)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    t.hello ("127.0.0.1", 10.0);
    EXPECT_EQ (t.takeRivals(), Strings { "127.0.0.1" });
}

TEST (MotionTarget, ASingleMotionHasNoRivals)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    t.hello (a3nuc2, 30.0);
    EXPECT_EQ (t.takeRivals(), Strings {});
}

// WhereMotionIsSaidToRun

TEST (MotionTarget, NoPlaceBeforeAnyHello)
{
    auto t = target();
    EXPECT_FALSE (t.followed().has_value());
    EXPECT_FALSE (t.follows ("127.0.0.1"));
}

TEST (MotionTarget, TheMotionFollowedHasAPlace)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    EXPECT_EQ (t.followed(), a3nuc2);
    EXPECT_TRUE (t.follows (a3nuc2));
}

TEST (MotionTarget, TheRigsOwnHasAPlaceToo)
{
    auto t = target();
    t.hello ("127.0.0.1", 0.0);
    EXPECT_TRUE (t.follows ("127.0.0.1"));
}

TEST (MotionTarget, ARivalHasNone)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    t.hello (notebook, 10.0);
    EXPECT_FALSE (t.follows (notebook));
}

TEST (MotionTarget, ASilenceTakesThePlaceAwayUntilTheNextHello)
{
    auto t = target();
    t.hello (a3nuc2, 0.0);
    t.silence (61.0);
    EXPECT_FALSE (t.followed().has_value());
    EXPECT_FALSE (t.follows (a3nuc2));
    t.hello (a3nuc2, 70.0);
    EXPECT_TRUE (t.follows (a3nuc2));
}

// AHostToTellAnotherMachine

TEST (MotionTarget, LoopbackIsTheCoreHost)
{
    EXPECT_EQ (hostToTellOf ("127.0.0.1", "192.168.8.10", own), "192.168.8.10");
}

TEST (MotionTarget, OneOfCoresOwnAddressesIsTheCoreHost)
{
    EXPECT_EQ (hostToTellOf ("192.168.8.10", "192.168.8.99", { "192.168.8.10" }), "192.168.8.99");
}

TEST (MotionTarget, AnotherMachineIsUnchanged)
{
    EXPECT_EQ (hostToTellOf (a3nuc2, "192.168.8.10", own), a3nuc2);
}
