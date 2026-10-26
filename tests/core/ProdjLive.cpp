// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_prodj_live.py (B0 f3345dc7feeb): pro-DJ LIVE is a lease
// Core holds -- off at start, repeated every five seconds while on.
#include <gtest/gtest.h>

#include "ProdjLive.h"

using namespace a3core;

namespace
{
ProdjLive liveOn()
{
    ProdjLive live;
    live.set (true, 0.0);
    return live;
}

const std::optional<std::string> noKind = std::nullopt;
}

// TheState

TEST (ProdjLive, StartsOff)
{
    EXPECT_FALSE (ProdjLive().on());
}

TEST (ProdjLive, SetReportsAChangeOnce)
{
    ProdjLive live;
    EXPECT_TRUE (live.set (true, 0.0));
    EXPECT_FALSE (live.set (true, 1.0));
    EXPECT_TRUE (live.on());
    EXPECT_TRUE (live.set (false, 2.0));
    EXPECT_FALSE (live.set (false, 3.0));
}

TEST (ProdjLive, TheLastProdjLeavingSwitchesItOff)
{
    auto live = liveOn();
    EXPECT_TRUE (live.nobodyLeft());
    EXPECT_FALSE (live.on());
    EXPECT_FALSE (live.nobodyLeft());
}

// TheLease

TEST (ProdjLive, RepeatsEveryFiveSecondsWhileOn)
{
    auto live = liveOn();
    EXPECT_FALSE (live.due (4.9));
    EXPECT_TRUE (live.due (5.0));
    EXPECT_FALSE (live.due (6.0));
    EXPECT_TRUE (live.due (10.0));
}

TEST (ProdjLive, NeverRepeatsWhileOff)
{
    ProdjLive live;
    for (double now : { 0.0, 5.0, 100.0 })
        EXPECT_FALSE (live.due (now));
}

TEST (ProdjLive, SwitchingOnAgainRestartsTheCount)
{
    auto live = liveOn();
    live.set (false, 1.0);
    live.set (true, 20.0);
    EXPECT_FALSE (live.due (24.9));
    EXPECT_TRUE (live.due (25.0));
}

// Admission

TEST (ProdjLive, AProdjMovesNothingWhileOff)
{
    ProdjLive live;
    EXPECT_FALSE (live.admits ("prodj", "channel.volume"));
    for (const char* key : { "device.hello", "state.recall", "prodj.live" })
        EXPECT_TRUE (live.admits ("prodj", key)) << key;
}

TEST (ProdjLive, AProdjMovesThingsWhileOn)
{
    auto live = liveOn();
    EXPECT_TRUE (live.admits ("prodj", "channel.volume"));
    EXPECT_TRUE (live.admits ("prodj", "stemdeck.pointer"));
    EXPECT_TRUE (live.admits ("prodj", "motion.pointer"));
}

TEST (ProdjLive, OthersAreAlwaysAdmitted)
{
    for (auto live : { ProdjLive(), liveOn() })
        for (const char* kind : { "desk", "motion" })
            EXPECT_TRUE (live.admits (kind, "channel.volume")) << kind << " on=" << live.on();
}

TEST (ProdjLive, NoKindAtAllIsAdmittedToo)
{
    // Motion's origin is the bare name "motion": kindOf() is nothing.
    for (auto live : { ProdjLive(), liveOn() })
        EXPECT_TRUE (live.admits (noKind, "channel.volume"));
}

TEST (ProdjLive, AFullNameIsReadByItsKind)
{
    ProdjLive live;
    EXPECT_FALSE (live.admits ("prodj@h", "channel.volume"));
    EXPECT_TRUE (live.admits ("desk@h", "channel.volume"));
    live.set (true, 0.0);
    EXPECT_TRUE (live.admits ("prodj@h", "motion.pointer"));
    EXPECT_FALSE (live.admits ("desk@h", "motion.pointer"));
}

TEST (ProdjLive, PointersPassOnlyFromAProdjWhileOn)
{
    for (auto live : { ProdjLive(), liveOn() })
        for (const auto& kind : { std::optional<std::string> ("desk"), std::optional<std::string> ("motion"), noKind })
            for (const char* key : { "stemdeck.pointer", "motion.pointer" })
                EXPECT_FALSE (live.admits (kind, key)) << key << " on=" << live.on();
}

// WhoSwitches

TEST (ProdjLive, MotionMayOnlySwitchOff)
{
    EXPECT_TRUE (maySwitch ("motion", false, true));
    EXPECT_FALSE (maySwitch ("motion", true, true));
    EXPECT_TRUE (maySwitch ("prodj", true, true));
    EXPECT_TRUE (maySwitch ("desk", true, true));
    EXPECT_FALSE (maySwitch ("mixer", true, true));
    EXPECT_FALSE (maySwitch ("mixer", false, true));
}

TEST (ProdjLive, AFullOriginNameCountsByItsKind)
{
    EXPECT_TRUE (maySwitch ("prodj@h", true, true));
    EXPECT_TRUE (maySwitch ("desk@h", false, true));
    EXPECT_TRUE (maySwitch ("motion", false, true));
    EXPECT_FALSE (maySwitch ("motion", true, true));
    EXPECT_FALSE (maySwitch ("mixer@h", true, true));
}

// OnlyWithAProdj

TEST (ProdjLive, OnIsRefusedWhileNoProdjIsFollowed)
{
    for (const char* origin : { "desk@h", "prodj@h", "desk", "prodj" })
        EXPECT_FALSE (maySwitch (origin, true, false)) << origin;
}

TEST (ProdjLive, OffIsAlwaysTaken)
{
    for (const char* origin : { "desk@h", "prodj@h", "motion" })
        EXPECT_TRUE (maySwitch (origin, false, false)) << origin;
}

TEST (ProdjLive, OnIsTakenWhileOneIsFollowed)
{
    EXPECT_TRUE (maySwitch ("desk@h", true, true));
}
