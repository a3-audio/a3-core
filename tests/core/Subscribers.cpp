// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_subscribers.py (B0 f3345dc7feeb): who gets told, and how
// a new one is added without touching the source.
#include <gtest/gtest.h>

#include <thread>

#include "B0Truth.h"
#include "Subscribers.h"

using namespace a3core;
using Names = std::vector<std::string>;

namespace
{
// Stands in for a WatchedClient, which is all everyoneBut reads.
Client named (const std::string& name) { return { name, { "127.0.0.1", 0 } }; }

Names namesOf (const std::vector<Client>& clients)
{
    Names names;
    for (const auto& client : clients)
        names.push_back (client.name);
    return names;
}

const Names reservedNames { "mixer", "motion", "reaper", "iem", "dualdelay" };
}

// OneSubscriber

TEST (Subscribers, ItReadsANameAHostAndAPort)
{
    EXPECT_EQ (parseSubscriber ("light=192.168.43.60:7771"), (Subscriber { "light", "192.168.43.60", 7771 }));
}

TEST (Subscribers, SpaceAroundTheNameIsForgiven)
{
    EXPECT_EQ (parseSubscriber (" video = 127.0.0.1:9100 "), (Subscriber { "video", "127.0.0.1", 9100 }));
}

TEST (Subscribers, AnIpv6HostKeepsItsColons)
{
    EXPECT_EQ (parseSubscriber ("light=::1:7771"), (Subscriber { "light", "::1", 7771 }));
}

// WhatIsRefused

namespace
{
void refuses (const std::string& text)
{
    EXPECT_THROW (parseSubscriber (text), SubscriberError) << text;
}
}

TEST (Subscribers, NoEqualsSign) { refuses ("192.168.43.60:7771"); }
TEST (Subscribers, NoName) { refuses ("=192.168.43.60:7771"); }
TEST (Subscribers, NoPort) { refuses ("light=192.168.43.60"); }
TEST (Subscribers, NoHost) { refuses ("light=:7771"); }
TEST (Subscribers, APortThatIsNotANumber) { refuses ("light=192.168.43.60:seven"); }

TEST (Subscribers, APortOutsideTheRange)
{
    refuses ("light=192.168.43.60:0");
    refuses ("light=192.168.43.60:99999");
}

// What the operator reads when Core refuses to start: the texts are the journal's.
TEST (Subscribers, TheRefusalsSayWhatIsWrong)
{
    const auto said = [] (const std::string& text) {
        try { parseSubscriber (text); } catch (const SubscriberError& problem) { return std::string (problem.what()); }
        return std::string();
    };
    EXPECT_EQ (said ("192.168.43.60:7771"), "--subscriber wants name=host:port, got '192.168.43.60:7771'");
    EXPECT_EQ (said ("=1.2.3.4:5"), "--subscriber has no name: '=1.2.3.4:5'");
    EXPECT_EQ (said ("light=1.2.3.4"), "--subscriber light wants host:port, got '1.2.3.4'");
    EXPECT_EQ (said ("light=:7771"), "--subscriber light has no host -- write 127.0.0.1:PORT");
    EXPECT_EQ (said ("light=1.2.3.4:seven"), "--subscriber light has no port number: 'seven'");
    EXPECT_EQ (said ("light=1.2.3.4:99999"), "--subscriber light port out of range: 99999");
    EXPECT_EQ (said ("light=1.2.3.4:0"), "--subscriber light port out of range: 0");
    EXPECT_EQ (said ("it's=1.2.3.4"), "--subscriber it's wants host:port, got '1.2.3.4'");
    EXPECT_EQ (said ("x'y"), "--subscriber wants name=host:port, got \"x'y\"");
}

// SeveralSubscribers

TEST (Subscribers, TheyKeepTheOrderTheyWereGiven)
{
    const auto found = parseSubscribers ({ "light=10.0.0.1:1", "video=10.0.0.2:2" });
    ASSERT_EQ (found.size(), 2u);
    EXPECT_EQ (found[0].name, "light");
    EXPECT_EQ (found[1].name, "video");
}

TEST (Subscribers, ANameCannotBeUsedTwice)
{
    EXPECT_THROW (parseSubscribers ({ "light=10.0.0.1:1", "light=10.0.0.2:2" }), SubscriberError);
}

TEST (Subscribers, AReservedNameIsRefused)
{
    for (const auto& name : reservedNames)
        EXPECT_THROW (parseSubscribers ({ name + "=10.0.0.1:1" }, reservedNames), SubscriberError) << name;
}

TEST (Subscribers, NothingGivenIsNothingAdded)
{
    EXPECT_TRUE (parseSubscribers ({}).empty());
}

// WhoHearsAValueThatJustArrived

namespace
{
const std::vector<Client> everyone { named ("mixer"), named ("motion"), named ("light") };

Names hearing (const std::optional<std::string>& origin) { return namesOf (everyoneBut (everyone, origin)); }
}

TEST (Subscribers, TheSenderIsLeftOut)
{
    EXPECT_EQ (hearing ("mixer"), (Names { "motion", "light" }));
}

TEST (Subscribers, AScreenThatSentItIsLeftOutToo)
{
    EXPECT_EQ (hearing ("motion"), (Names { "mixer", "light" }));
}

TEST (Subscribers, WithNoKnownSenderEverybodyHearsIt)
{
    for (const auto& origin : { std::optional<std::string> {}, std::optional<std::string> (""), std::optional<std::string> ("reaper"),
                                std::optional<std::string> ("127.0.0.1 (mehrdeutig)") })
        EXPECT_EQ (hearing (origin), (Names { "mixer", "motion", "light" })) << origin.value_or ("None");
}

TEST (Subscribers, TheOrderIsTheSubscriberOrder)
{
    EXPECT_EQ (hearing ("light"), (Names { "mixer", "motion" }));
}

// AMotionThatMoves

TEST (Subscribers, ItTakesTheSlotOfItsName)
{
    auto list = everyone;
    auto moved = named ("motion");
    moved.to = { "192.168.8.20", 7771 };
    replaceNamed (list, moved);
    EXPECT_EQ (list[1].to, moved.to);
    EXPECT_EQ (namesOf (list), (Names { "mixer", "motion", "light" }));
}

TEST (Subscribers, ANameNotInTheListIsRefused)
{
    auto list = everyone;
    EXPECT_THROW (replaceNamed (list, named ("video")), std::out_of_range);
}

// WhatIsPassedOnWhenItArrives

namespace
{
bool relayed (const std::string& address) { return relayOnArrival (b0::truth(), address); }
}

TEST (Subscribers, TheChannelStripIsPassedOn)
{
    for (const char* parameter : { "gain", "volume", "aux-send", "3d" })
        EXPECT_TRUE (relayed (std::string ("/channel/1/") + parameter)) << parameter;
}

TEST (Subscribers, TheEqBandsArePassedOn)
{
    for (const char* band : { "high", "mid", "low" })
        EXPECT_TRUE (relayed (std::string ("/channel/3/eq/") + band)) << band;
}

TEST (Subscribers, TheMasterAndTheFilterArePassedOn)
{
    for (const char* address : { "/master/volume", "/master/booth", "/master/phones-mix", "/master/phones-volume",
                                 "/master/aux-return", "/filter/frequency", "/filter/resonance" })
        EXPECT_TRUE (relayed (address)) << address;
}

TEST (Subscribers, AFlagIsNotPassedOnHere)
{
    for (const char* address : { "/channel/1/cue", "/channel/4/filter", "/filter/mode" })
        EXPECT_FALSE (relayed (address)) << address;
}

TEST (Subscribers, ThePositionIsNotPassedOn)
{
    for (const char* address : { "/channel/1/azimuth", "/channel/2/elevation", "/stemdeck/pointer", "/motion/pointer" })
        EXPECT_FALSE (relayed (address)) << address;
}

TEST (Subscribers, AnAccentStillReachesTheOtherScreens)
{
    EXPECT_TRUE (relayed ("/channel/1/filter/frequency"));
    EXPECT_TRUE (relayed ("/channel/1/filter/q"));
}

TEST (Subscribers, WhatIsNotAValueAtAll)
{
    for (const char* address : { "/beat", "/tap", "/state/recall", "/channel/1", "/channel", "/track/12/fx/1/fxparam/1/value", "/" })
        EXPECT_FALSE (relayed (address)) << address;
}

TEST (Subscribers, TheOldNamesAreNotPassedOn)
{
    for (const char* address : { "/channel/0/volume", "/fx/frequency", "/master/return", "/master/phones_mix", "/channel/1/pot_1" })
        EXPECT_FALSE (relayed (address)) << address;
}

// TheTwoThatShip

TEST (Subscribers, TheMixerComesBeforeMotion)
{
    EXPECT_EQ (shipped, (std::vector<std::string> { "mixer", "motion" }));
}

// GrowingAndShrinking

TEST (Subscribers, ADeskIsAddedAfterTheOnesThatShip)
{
    std::vector<Client> list { named ("mixer"), named ("motion") };
    addSubscriber (list, named ("desk@192.168.8.20"));
    EXPECT_EQ (namesOf (list), (Names { "mixer", "motion", "desk@192.168.8.20" }));
}

TEST (Subscribers, ANameIsAddedOnce)
{
    std::vector<Client> list { named ("desk@192.168.8.20") };
    EXPECT_THROW (addSubscriber (list, named ("desk@192.168.8.20")), std::invalid_argument);
    try { addSubscriber (list, named ("desk@192.168.8.20")); }
    catch (const std::invalid_argument& problem) { EXPECT_STREQ (problem.what(), "there is already a desk@192.168.8.20"); }
}

TEST (Subscribers, DroppingKeepsTheSameList)
{
    std::vector<Client> list { named ("mixer"), named ("desk@192.168.8.20") };
    const auto* held = list.data();
    dropNamed (list, "desk@192.168.8.20");
    EXPECT_EQ (list.data(), held);   // the one list every handler holds, changed in place
    EXPECT_EQ (namesOf (list), (Names { "mixer" }));
}

TEST (Subscribers, DroppingANameNotThereIsNothing)
{
    std::vector<Client> list { named ("mixer") };
    dropNamed (list, "desk@192.168.8.99");
    EXPECT_EQ (namesOf (list), (Names { "mixer" }));
}

// TheOwningThread

TEST (Subscribers, TheClaimingThreadPasses)
{
    OwningThread owner;
    owner.claim();
    EXPECT_NO_THROW (owner.check ("a desk joining"));
}

TEST (Subscribers, AnotherThreadIsRefusedAndNamed)
{
    OwningThread owner;
    owner.claim();
    std::vector<std::string> caught;
    std::thread other ([&] {
        try { owner.check ("a desk joining"); }
        catch (const std::runtime_error& problem) { caught.push_back (problem.what()); }
    });
    other.join();
    ASSERT_EQ (caught.size(), 1u);
    EXPECT_EQ (caught[0], "a desk joining on a thread that does not own the subscribers");
}

TEST (Subscribers, NothingClaimedIsRefused)
{
    OwningThread owner;
    try
    {
        owner.check ("a desk leaving");
        FAIL() << "expected a refusal";
    }
    catch (const std::runtime_error& problem)
    {
        EXPECT_EQ (std::string (problem.what()), "a desk leaving: no thread owns the subscribers yet");
    }
}
