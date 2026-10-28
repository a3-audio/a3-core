// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Who Core follows and what it tells them: the hellos, Motion, the desks and pro-DJ apps, LIVE and the
// pointers, the beat, StemDeck's preview and the meters, the decks' own ports -- driven through
// a3core::Core on a fake network. Ports of B1's (a3-core 7a7b2441f74e) test_core_motion_wiring,
// test_core_desk_wiring, test_core_prodj_wiring, test_core_one_stemdeck_wiring, test_core_deck_ports
// (DeckPortsInCore), test_core_ahead (CoreIsWired) and test_core_vu_relay (CoreRunsIt). Where the
// Python test read a3-core.py's source, the port holds the behaviour that source text was there for.
#include <gtest/gtest.h>

#include <stdexcept>
#include <thread>

#include "CoreRig.h"

using namespace corerig;

namespace
{
Endpoint deskAt (int lastByte) { return { "192.168.8." + std::to_string (lastByte), 7773 }; }

std::string deskHost (int lastByte) { return "192.168.8." + std::to_string (lastByte); }

const std::string coreHost = "192.168.8.10";

// A packet handled on a thread that is not the serve thread; what it threw, or empty.
std::string onAnotherThread (const std::function<void()>& work)
{
    std::string thrown;
    std::thread other ([&] {
        try
        {
            work();
        }
        catch (const std::exception& problem)
        {
            thrown = problem.what();
        }
    });
    other.join();
    return thrown;
}

std::vector<uint8_t> meterBundle()
{
    // Bytes the relay does not read: they go on as they came.
    return { '#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1, 0xde, 0xad };
}

// A StemDeck meter message (/vu/41, ff): a datagram Core can tell the source of. Where a test only
// follows the bytes, meterBundle() is enough; one that asserts nothing was left unknown needs this.
std::vector<uint8_t> stemMeterBundle() { return encodeMessage ({ "/vu/41", { pyFloat (0.5), pyFloat (0.25) } }); }

// The /prodj/live sends of every client whose name starts with `prefix`, as "client host:port".
std::vector<std::string> liveTargets (const CoreRig& rig, const std::string& prefix)
{
    std::vector<std::string> out;
    for (const auto& entry : rig.net.transcript)
        if (entry["address"].toString() == "/prodj/live" && entry["to"].toString().startsWith (juce::String (prefix)))
            out.push_back (entry["to"].toString().toStdString() + " " + entry["host"].toString().toStdString() + ":"
                           + entry["port"].toString().toStdString());
    std::sort (out.begin(), out.end());
    return out;
}

void prodjJoinsAndSwitchesLiveOn (CoreRig& rig, int lastByte = 60)
{
    rig.hello (deskAt (lastByte), "prodj");
    rig.osc (deskAt (lastByte), "/prodj/live", { pyInt (1) });
}
}

//==============================================================================
// test_core_ahead.CoreIsWired

TEST (CorePeers, TheStemdeckHandlerPassesItOn)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.clear();
    rig.osc ({ "127.0.0.2", 6000 }, "/stemdeck/ahead", { pyStr ("groove"), pyStr ("build"), pyInt (8), pyInt (1) });
    EXPECT_EQ (rig.to ("motion"), (std::vector<std::string> { "/stemdeck/ahead s:groove s:build i:8 f:1" }));
}

TEST (CorePeers, OnlyFromTheFollowedStemdeck)
{
    CoreRig rig;
    const std::vector<OscArg> preview { pyStr ("drop"), pyStr ("end"), pyInt (-1), pyFloat (0.9) };
    rig.osc ({ "127.0.0.2", 6000 }, "/stemdeck/ahead", preview);   // no StemDeck followed yet
    rig.stemdeckHello ("127.0.0.2");
    rig.osc ({ "192.168.8.30", 6000 }, "/stemdeck/ahead", preview);   // not the one followed
    EXPECT_TRUE (startingWith (rig.to ("motion"), "/stemdeck/ahead").empty());
    rig.osc ({ "127.0.0.2", 6000 }, "/stemdeck/ahead", preview);
    EXPECT_EQ (startingWith (rig.to ("motion"), "/stemdeck/ahead").size(), 1u);
}

TEST (CorePeers, ItGoesToMotionOnly)
{
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.stemdeckHello ("127.0.0.2");
    rig.clear();
    rig.osc ({ "127.0.0.2", 6000 }, "/stemdeck/ahead", { pyStr ("drop"), pyStr ("end"), pyInt (-1), pyFloat (0.9) });
    EXPECT_EQ (rig.sendLines(), (std::vector<std::string> { "motion /stemdeck/ahead s:drop s:end i:-1 f:0.9" }));
}

TEST (CorePeers, ItIsNeverNotedAndSoNeverReplayed)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.stemdeckHello ("127.0.0.2");
    const std::vector<OscArg> preview { pyStr ("groove"), pyStr ("build"), pyInt (8), pyFloat (0.6) };
    rig.osc ({ "127.0.0.2", 6000 }, "/stemdeck/ahead", preview);
    rig.osc ({ "127.0.0.2", 6000 }, "/stemdeck/ahead", preview);   // a repeat goes on: it is not the relay
    EXPECT_EQ (startingWith (rig.to ("motion"), "/stemdeck/ahead").size(), 2u);
    rig.clear();
    rig.osc (rig.motion, "/state/recall");
    rig.engineHello ("boot-2");
    for (const auto& line : rig.sendLines())
        EXPECT_EQ (line.find ("/stemdeck/ahead"), std::string::npos) << line;
    rig.stop();
    EXPECT_FALSE (rig.file ("evening.json").loadFileAsString().contains ("ahead"));
}

TEST (CorePeers, AMalformedOneIsShownAsUnknown)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.clear();
    rig.osc ({ "127.0.0.2", 6000 }, "/stemdeck/ahead", { pyStr ("drop"), pyInt (3) });
    EXPECT_EQ (rig.unknownCount ("/stemdeck/ahead"), 1);
    EXPECT_EQ (rig.sends(), 0u);
}

//==============================================================================
// test_core_desk_wiring

TEST (CorePeers, IsFollowedOnlyWithCoresOwnTruth)
{
    CoreRig rig;
    rig.hello (deskAt (41), "desk", otherHash);
    rig.hello (deskAt (61), "prodj", otherHash);
    EXPECT_TRUE (rig.to ("desk@" + deskHost (41)).empty());
    EXPECT_TRUE (rig.to ("prodj@" + deskHost (61)).empty());
    rig.hello (deskAt (41), "desk");
    EXPECT_FALSE (rig.to ("desk@" + deskHost (41)).empty());
}

TEST (CorePeers, AJoiningDeskBecomesASubscriberAndHearsEverything)
{
    CoreRig rig;
    rig.clear();
    rig.hello (deskAt (40), "desk");
    const auto toDesk = rig.to ("desk@" + deskHost (40));
    auto whole = rig.to ("mixer");   // the whole state, said to every subscriber at once
    whole.push_back ("/prodj/live i:0");
    EXPECT_EQ (toDesk, whole);
    EXPECT_TRUE (has (rig.journal(), "desk: " + deskHost (40) + " joined, 1 of 4; replayed 19 messages"));
    rig.clear();
    rig.osc (rig.desk, "/channel/2/gain", { pyStr ("0.6") });
    EXPECT_EQ (rig.to ("desk@" + deskHost (40)), (std::vector<std::string> { "/channel/2/gain f:0.6" }));
}

TEST (CorePeers, AFifthDeskIsSaidInTheJournal)
{
    CoreRig rig;
    for (int desk = 40; desk <= 44; ++desk)
        rig.hello (deskAt (desk), "desk");
    rig.hello (deskAt (44), "desk");
    EXPECT_EQ (count (rig.journal(), "desk: 192.168.8.44 said hello, but 4 desks are followed already (192.168.8.40, "
                                     "192.168.8.41, 192.168.8.42, 192.168.8.43) -- not followed"),
               1);
    EXPECT_TRUE (rig.to ("desk@" + deskHost (44)).empty());
}

TEST (CorePeers, TheWindowNamesEachDeskByItsHost)
{
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.hello (deskAt (41), "desk");
    EXPECT_TRUE (has (rig.journal(), "desk@192.168.8.40: its a3-osc.json is Core's (" + ownHash().substr (0, 12) + ")"));
    EXPECT_TRUE (has (rig.journal(), "desk@192.168.8.41: its a3-osc.json is Core's (" + ownHash().substr (0, 12) + ")"));
    const auto devices = rig.core->devices().snapshot (rig.clock.monotonic());
    ASSERT_EQ (devices.size(), 2);
    EXPECT_EQ (devices[0]["device"].toString(), "desk@192.168.8.40");
    EXPECT_EQ (devices[1]["device"].toString(), "desk@192.168.8.41");
}

TEST (CorePeers, BroadcastWalksASnapshot)
{
    // A desk joining while a broadcast is under way (on the rig: another thread's view of the list)
    // neither cuts the broadcast short nor is told half of it.
    CoreRig rig;
    rig.osc (rig.desk, "/channel/1/cue", { pyStr ("1") });
    rig.clear();
    bool joined = false;
    rig.net.sent = [&] (const Client& client, const OscMessage& message) {
        if (joined || client.name != "mixer" || message.address != "/channel/1/cue/led")
            return;
        joined = true;
        const auto data = encodeMessage ({ "/device/hello", { pyStr ("desk"), pyStr (ownHash()) } });
        rig.core->handlePacket (deskAt (40), data.data(), data.size());
    };
    rig.osc (rig.desk, "/channel/1/cue", { pyStr ("1") });
    rig.net.sent = nullptr;
    ASSERT_TRUE (joined);
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/1/cue/led f:0"));
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/1/cue f:0"));
}

TEST (CorePeers, ASilentDeskLeavesTheSubscribers)
{
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.at (60.0);
    rig.tick();
    EXPECT_FALSE (has (rig.journal(), "desk: 192.168.8.40 silent for a minute, no longer followed"));
    rig.at (60.2);
    rig.tick();
    EXPECT_EQ (count (rig.journal(), "desk: 192.168.8.40 silent for a minute, no longer followed"), 1);
    rig.clear();
    rig.osc (rig.desk, "/channel/2/gain", { pyStr ("0.7") });
    EXPECT_TRUE (rig.to ("desk@" + deskHost (40)).empty());
    EXPECT_FALSE (rig.to ("motion").empty());
}

TEST (CorePeers, IsWatchedOnEveryTick)
{
    // Nothing else arrives: the tick alone notices the silence.
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.at (61.0);
    rig.tick();
    EXPECT_TRUE (has (rig.journal(), "desk: 192.168.8.40 silent for a minute, no longer followed"));
}

TEST (CorePeers, ADeskIsKnownByItsSocketBeforeItsHost)
{
    // A desk beside Motion on one machine: its moves must reach Motion, not be taken for Motion's own.
    CoreRig rig;
    rig.motionHello();
    rig.hello ({ "127.0.0.1", 7773 }, "desk");
    rig.clear();
    rig.osc ({ "127.0.0.1", 7773 }, "/channel/1/gain", { pyStr ("0.3") });
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/1/gain f:0.3"));
    EXPECT_TRUE (rig.to ("desk@127.0.0.1").empty());
    EXPECT_EQ (rig.peerOf ("/channel/1/gain"), "desk@127.0.0.1");
    rig.osc (rig.motion, "/channel/1/gain", { pyFloat (0.4) });
    EXPECT_EQ (rig.peerOf ("/channel/1/gain"), "motion");
}

TEST (CorePeers, ChannelOriginComesFromOriginOf)
{
    CoreRig rig;
    rig.motionHello();
    const Endpoint deskBesideMotion { "127.0.0.1", 7773 };
    rig.hello (deskBesideMotion, "desk");
    for (const auto* address : { "/channel/1/volume", "/master/volume", "/aux-return/stem/push", "/filter/frequency" })
    {
        rig.osc (deskBesideMotion, address, { pyStr ("1") });
        EXPECT_EQ (rig.peerOf (address), "desk@127.0.0.1") << address;
    }
}

TEST (CorePeers, TheTapsAndJournalNameADeskByItsSocket)
{
    CoreRig rig;
    rig.motionHello();
    const Endpoint deskBesideMotion { "127.0.0.1", 7773 };
    rig.hello (deskBesideMotion, "desk");
    EXPECT_EQ (rig.peerOf ("/device/hello"), "motion");   // seen before the desk is followed
    rig.hello (deskBesideMotion, "desk");
    EXPECT_EQ (rig.peerOf ("/device/hello"), "desk@127.0.0.1");
    rig.osc (deskBesideMotion, "/state/recall");
    EXPECT_EQ (rig.peerOf ("/state/recall"), "desk@127.0.0.1");
    rig.hello (deskBesideMotion, "desk", otherHash);   // still followed, its values shut out
    rig.osc (deskBesideMotion, "/channel/1/gain", { pyStr ("0.3") });
    EXPECT_TRUE (has (rig.journal(), "desk@127.0.0.1 (127.0.0.1): values ignored, its a3-osc.json differs from Core's"));
}

TEST (CorePeers, EveryDeskGetsTheBeat)
{
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.hello (deskAt (41), "desk");
    rig.clear();
    rig.beat ({ pyInt (2), pyInt (1), pyFloat (120.0) });
    EXPECT_EQ (rig.to ("desk@192.168.8.40"), (std::vector<std::string> { "/beat i:2 i:1 f:120" }));
    EXPECT_EQ (rig.to ("desk@192.168.8.41"), (std::vector<std::string> { "/beat i:2 i:1 f:120" }));
}

TEST (CorePeers, TheRelayFeedsTheMotionAndEveryDesk)
{
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.hello (rig.remoteMotion, "motion");
    rig.clear();
    rig.vu (meterBundle());
    EXPECT_EQ (rig.targetsOf ("vu"), (std::vector<std::string> { "192.168.8.20:7772", "192.168.8.40:7774" }));
}

TEST (CorePeers, JoiningChecksTheOwnerBeforeAskingTheDesks)
{
    CoreRig rig;
    const auto data = encodeMessage ({ "/device/hello", { pyStr ("desk"), pyStr (ownHash()) } });
    const auto thrown = onAnotherThread ([&] { rig.core->handlePacket (deskAt (40), data.data(), data.size()); });
    EXPECT_EQ (thrown, "a desk joining on a thread that does not own the subscribers");
    // Refused before the desks were asked: on the serve thread the same hello is still a join.
    rig.hello (deskAt (40), "desk");
    EXPECT_TRUE (has (rig.journal(), "desk: 192.168.8.40 joined, 1 of 4; replayed 19 messages"));
}

TEST (CorePeers, LeavingChecksTheOwnerBeforeAskingTheSilence)
{
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.at (61.0);
    const auto thrown = onAnotherThread ([&] { rig.core->tick(); });
    EXPECT_EQ (thrown, "a desk leaving on a thread that does not own the subscribers");
    rig.tick();
    EXPECT_TRUE (has (rig.journal(), "desk: 192.168.8.40 silent for a minute, no longer followed"));
}

TEST (CorePeers, TheOwnerLivesBesideTheDesks)
{
    // One owner for the list, whoever asks: a join and a leave are refused off the serve thread alike.
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    const auto data = encodeMessage ({ "/device/hello", { pyStr ("desk"), pyStr (ownHash()) } });
    EXPECT_NE (onAnotherThread ([&] { rig.core->handlePacket (deskAt (41), data.data(), data.size()); }), "");
    EXPECT_NE (onAnotherThread ([&] { rig.core->tick(); }), "");
}

TEST (CorePeers, TheServeThreadClaimsTheListJustBeforeServing)
{
    CoreRig::Options options;
    options.start = false;
    CoreRig rig (CoreRig::WithOptions {}, options);
    EXPECT_THROW (rig.core->tick(), std::runtime_error);   // nobody owns the list before start()
    rig.start();
    EXPECT_NO_THROW (rig.tick());
}

//==============================================================================
// test_core_devices (Core listens and compares) is in CoreMaps.cpp.

//==============================================================================
// test_core_motion_wiring

TEST (CorePeers, MotionSaysHello)
{
    const auto data = b0::truth().data();   // held: the array lives in it
    const auto* from = data["addresses"]["device.hello"]["from"].getArray();
    ASSERT_NE (from, nullptr);
    EXPECT_TRUE (from->contains ("motion"));
}

TEST (CorePeers, IsHandedToTheTarget)
{
    CoreRig rig;
    rig.hello (rig.remoteMotion, "motion");
    rig.clear();
    rig.osc (rig.desk, "/channel/1/gain", { pyStr ("0.5") });
    EXPECT_EQ (rig.targetsOf ("motion"), (std::vector<std::string> { "192.168.8.20:7771" }));
}

TEST (CorePeers, ItsSilenceIsWatchedOnEveryTick)
{
    CoreRig rig;
    rig.hello (rig.remoteMotion, "motion");
    rig.at (60.5);
    rig.tick();
    EXPECT_TRUE (has (rig.journal(), "motion: the remote one is silent, back to the rig's, now 127.0.0.1:7771; replayed 19 messages"));
}

TEST (CorePeers, TakesMotionsSlotAmongTheSubscribers)
{
    CoreRig rig;
    rig.hello (rig.remoteMotion, "motion");
    rig.clear();
    rig.osc (rig.desk, "/channel/2/gain", { pyStr ("0.25") });
    EXPECT_EQ (rig.sendLines(), (std::vector<std::string> { "motion /channel/2/gain f:0.25" }));
    EXPECT_EQ (rig.targetsOf ("motion"), (std::vector<std::string> { "192.168.8.20:7771" }));
}

TEST (CorePeers, NamesTheNewHostMotionInTheWindow)
{
    CoreRig rig;
    rig.osc (rig.remoteMotion, "/state/recall");
    EXPECT_EQ (rig.peerOf ("/state/recall"), "192.168.8.20");
    rig.hello (rig.remoteMotion, "motion");
    rig.osc (rig.remoteMotion, "/state/recall");
    EXPECT_EQ (rig.peerOf ("/state/recall"), "motion");
}

TEST (CorePeers, SaysTheWholeStateToIt)
{
    CoreRig rig;
    rig.clear();
    rig.hello (rig.remoteMotion, "motion");
    auto whole = rig.to ("mixer");
    whole.push_back ("/prodj/live i:0");
    EXPECT_EQ (rig.to ("motion"), whole);
    EXPECT_TRUE (has (rig.journal(), "motion: said hello, now 192.168.8.20:7771; replayed 19 messages"));
}

TEST (CorePeers, ARecallSaysTheSameWholeState)
{
    CoreRig rig;
    rig.clear();
    rig.hello (rig.remoteMotion, "motion");
    const auto onTheSwitch = rig.to ("motion");
    rig.clear();
    rig.osc (rig.remoteMotion, "/state/recall");
    EXPECT_EQ (rig.to ("motion"), onTheSwitch);   // the whole state, then LIVE to the asker
}

TEST (CorePeers, BothTheHelloAndTheSilenceSwitch)
{
    CoreRig rig;
    rig.hello (rig.remoteMotion, "motion");
    rig.at (61.0);
    rig.tick();
    const auto lines = startingWith (rig.journal(), "motion: ");
    EXPECT_TRUE (has (lines, "motion: said hello, now 192.168.8.20:7771; replayed 19 messages"));
    EXPECT_TRUE (has (lines, "motion: the remote one is silent, back to the rig's, now 127.0.0.1:7771; replayed 19 messages"));
    rig.clear();
    rig.osc (rig.desk, "/channel/2/gain", { pyStr ("0.25") });
    EXPECT_EQ (rig.targetsOf ("motion"), (std::vector<std::string> { "127.0.0.1:7771" }));
}

TEST (CorePeers, ARivalIsSaidInTheJournalTwoMotions)
{
    CoreRig rig;
    rig.motionHello();
    rig.hello (rig.remoteMotion, "motion");
    rig.hello (rig.remoteMotion, "motion");
    EXPECT_EQ (count (rig.journal(), "motion: a second Motion at 192.168.8.20 while following 127.0.0.1 -- only one may run; "
                                     "still following 127.0.0.1"),
               1);
    rig.clear();
    rig.osc (rig.desk, "/channel/2/gain", { pyStr ("0.25") });
    EXPECT_EQ (rig.targetsOf ("motion"), (std::vector<std::string> { "127.0.0.1:7771" }));
}

TEST (CorePeers, TheRivalIsNamedBeforeTheEarlyReturnTwoMotions)
{
    // The rival's hello moves nothing; it is said all the same, and Motion followed hears LIVE.
    CoreRig rig;
    rig.motionHello();
    rig.clear();
    rig.hello (rig.remoteMotion, "motion");
    EXPECT_EQ (startingWith (rig.journal(), "motion: a second Motion").size(), 1u);
    EXPECT_EQ (rig.sendLines(), (std::vector<std::string> { "motion /prodj/live i:0" }));
}

TEST (CorePeers, TheStemdeckWatchIsToldWhatIsLocal)
{
    // A StemDeck on another machine keeps the lead over the rig's own: after its silence the rig's
    // hello only notices the silence, and the next one is followed.
    CoreRig rig;
    rig.stemdeckHello ("192.168.8.30");
    rig.at (61.0);
    rig.clear();
    rig.stemdeckHello ("127.0.0.2");
    EXPECT_TRUE (rig.to ("stemdeck").empty());
    EXPECT_TRUE (has (rig.journal(), "stemdeck: silent for a minute, its buses are empty"));
    rig.at (61.5);
    rig.stemdeckHello ("127.0.0.2");
    EXPECT_EQ (rig.to ("stemdeck"), (std::vector<std::string> { "/stemdeck/recall i:1", "/prodj/live i:0" }));
}

TEST (CorePeers, GoesOnToTheFollowedMotionWhenItIsRemote)
{
    CoreRig rig;
    rig.motionHello();
    rig.clear();
    rig.beat ({ pyInt (1), pyInt (1), pyFloat (120.0) });
    EXPECT_TRUE (rig.to ("motion").empty());   // the analyzer reaches the rig's own directly
    rig.at (61.0);
    rig.tick();
    rig.hello (rig.remoteMotion, "motion");
    rig.clear();
    rig.beat ({ pyInt (2), pyInt (1), pyFloat (120.0) });
    EXPECT_EQ (rig.to ("motion"), (std::vector<std::string> { "/beat i:2 i:1 f:120" }));
}

TEST (CorePeers, GoesOnBeforeTheTempoFilterCanReturn)
{
    CoreRig rig;
    rig.hello (rig.remoteMotion, "motion");
    rig.clear();
    rig.beat ({ pyInt (4), pyInt (1) });                       // no tempo at all
    rig.beat ({ pyInt (1), pyInt (2), pyFloat (5000.0) });     // no tempo the delay takes
    rig.beat ({ pyInt (2), pyInt (2), pyFloat (120.0) });      // a tempo not steady yet
    EXPECT_EQ (rig.to ("motion"), (std::vector<std::string> { "/beat i:4 i:1", "/beat i:1 i:2 f:5000", "/beat i:2 i:2 f:120" }));
}

TEST (CorePeers, AHelloOpensTheGateOnlyAfterMotionIsTold)
{
    // B1's order in osc_handler_device_hello: motion_said_hello (the whole state to a Motion that
    // moved), then the gate. A value of that Motion that arrives while it is being told is refused.
    CoreRig rig;
    bool arrived = false;
    rig.net.sent = [&] (const Client& client, const OscMessage&) {
        if (arrived || client.name != "motion" || client.to.host != rig.remoteMotion.host)
            return;
        arrived = true;
        const auto data = encodeMessage ({ "/channel/1/gain", { pyFloat (0.45) } });
        rig.core->handlePacket (rig.remoteMotion, data.data(), data.size());
    };
    rig.hello (rig.remoteMotion, "motion");
    rig.net.sent = nullptr;
    ASSERT_TRUE (arrived);
    EXPECT_FALSE (has (rig.to ("mixer"), "/channel/1/gain f:0.45"));
    EXPECT_TRUE (has (rig.journal(), "motion (192.168.8.20): values before its hello ignored"));
}

//==============================================================================
// test_core_one_stemdeck_wiring

TEST (CorePeers, TheWatchFollowsOneAtATime)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.clear();
    rig.stemdeckHello ("192.168.8.30");
    EXPECT_TRUE (rig.to ("stemdeck").empty());
    EXPECT_TRUE (rig.targetsOf ("stemdeck").empty());
}

TEST (CorePeers, ARivalIsSaidInTheJournalTwoStemDecks)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.stemdeckHello ("192.168.8.30");
    rig.stemdeckHello ("192.168.8.30");
    EXPECT_EQ (startingWith (rig.journal(), "stemdeck: a second StemDeck at 192.168.8.30").size(), 1u);
}

TEST (CorePeers, TheRivalIsNamedBeforeTheEarlyReturnTwoStemDecks)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.clear();
    rig.stemdeckHello ("192.168.8.30");   // not news for the link, said all the same
    EXPECT_EQ (startingWith (rig.journal(), "stemdeck: a second StemDeck").size(), 1u);
    EXPECT_EQ (rig.sends(), 0u);
}

TEST (CorePeers, ItNamesTheHostFollowed)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.stemdeckHello ("192.168.8.30");
    EXPECT_TRUE (has (rig.journal(), "stemdeck: a second StemDeck at 192.168.8.30 while following 127.0.0.2 -- only one may run; "
                                     "still following 127.0.0.2"));
}

//==============================================================================
// test_core_prodj_wiring.Joining

TEST (CorePeers, AProdjHelloJoinsAsADeskOfItsKind)
{
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");
    EXPECT_TRUE (has (rig.journal(), "desk: 192.168.8.60 joined, 1 of 4; replayed 19 messages"));
    EXPECT_FALSE (rig.to ("prodj@192.168.8.60").empty());
}

TEST (CorePeers, AJoiningProdjIsToldPositionsPlacesAndLive)
{
    CoreRig rig;
    rig.motionHello();
    rig.stemdeckHello ("127.0.0.2");
    rig.osc (rig.desk, "/channel/1/azimuth", { pyStr ("45") });
    rig.clear();
    rig.hello (deskAt (60), "prodj");
    const auto told = rig.to ("prodj@192.168.8.60");
    EXPECT_TRUE (has (told, "/channel/1/azimuth f:45"));
    const std::vector<std::string> tail (told.end() - 3, told.end());
    EXPECT_EQ (tail, (std::vector<std::string> { "/core/device/at s:motion s:" + coreHost, "/core/device/at s:stemdeck s:" + coreHost,
                                                 "/prodj/live i:0" }));
}

TEST (CorePeers, TheClientIsNamedByItsKind)
{
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");
    rig.hello (deskAt (40), "desk");
    EXPECT_FALSE (rig.to ("prodj@192.168.8.60").empty());
    EXPECT_TRUE (rig.to ("desk@192.168.8.60").empty());
    EXPECT_FALSE (rig.to ("desk@192.168.8.40").empty());
}

TEST (CorePeers, TheProdjClientsAreTheDesksOfThatKind)
{
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");
    rig.hello (deskAt (40), "desk");
    rig.clear();
    rig.osc (rig.desk, "/channel/1/azimuth", { pyStr ("45") });
    EXPECT_EQ (rig.sendLines(), (std::vector<std::string> { "prodj@192.168.8.60 /channel/1/azimuth f:45" }));
}

TEST (CorePeers, ARecallAnswersTheAskerWithLive)
{
    CoreRig rig;
    rig.motionHello();
    rig.hello (deskAt (40), "desk");
    rig.hello (deskAt (60), "prodj");
    rig.clear();
    rig.osc (deskAt (40), "/state/recall");
    EXPECT_EQ (rig.to ("desk@192.168.8.40").back(), "/prodj/live i:0");
    EXPECT_EQ (count (rig.to ("desk@192.168.8.40"), "/prodj/live i:0"), 1);
    rig.clear();
    rig.osc (deskAt (60), "/state/recall");
    const auto told = rig.to ("prodj@192.168.8.60");
    const std::vector<std::string> tail (told.end() - 2, told.end());
    EXPECT_EQ (tail, (std::vector<std::string> { "/core/device/at s:motion s:" + coreHost, "/prodj/live i:0" }));
    EXPECT_TRUE (rig.to ("desk@192.168.8.40").back() != "/prodj/live i:0");   // only the asker
}

TEST (CorePeers, StemdeckNewsTellsItLive)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    EXPECT_EQ (rig.to ("stemdeck"), (std::vector<std::string> { "/stemdeck/recall i:1", "/prodj/live i:0" }));
}

//==============================================================================
// test_core_prodj_wiring.WhatAProdjIsTold

TEST (CorePeers, TellProdjSendsPlacesAndLive)
{
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");
    rig.clear();
    rig.osc (deskAt (60), "/state/recall");   // nothing heard: no place, LIVE only
    EXPECT_EQ (rig.to ("prodj@192.168.8.60").back(), "/prodj/live i:0");
    EXPECT_TRUE (startingWith (rig.to ("prodj@192.168.8.60"), "/core/device/at").empty());
}

TEST (CorePeers, ThePositionsReachAJoiningOrRecallingProdjOnce)
{
    CoreRig rig;
    rig.osc (rig.desk, "/channel/1/azimuth", { pyStr ("45") });
    rig.clear();
    rig.hello (deskAt (60), "prodj");
    EXPECT_EQ (count (rig.to ("prodj@192.168.8.60"), "/channel/1/azimuth f:45"), 1);
    rig.clear();
    rig.osc (deskAt (60), "/state/recall");
    EXPECT_EQ (count (rig.to ("prodj@192.168.8.60"), "/channel/1/azimuth f:45"), 1);
}

TEST (CorePeers, PositionsReachTheProdjAfterIem)
{
    // The clamped value, as told to the engine: what arrived is not what is played.
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");
    rig.clear();
    rig.osc (rig.desk, "/channel/2/azimuth", { pyStr ("200") });
    rig.osc (rig.desk, "/channel/2/elevation", { pyStr ("-95") });
    EXPECT_EQ (rig.to ("prodj@192.168.8.60"), (std::vector<std::string> { "/channel/2/azimuth f:180", "/channel/2/elevation f:-90" }));
}

TEST (CorePeers, MotionAndStemdeckMovingTellTheProdj)
{
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");
    rig.clear();
    rig.hello (rig.remoteMotion, "motion");
    rig.stemdeckHello ("192.168.8.30");
    EXPECT_TRUE (has (rig.to ("prodj@192.168.8.60"), "/core/device/at s:motion s:192.168.8.20"));
    EXPECT_TRUE (has (rig.to ("prodj@192.168.8.60"), "/core/device/at s:stemdeck s:192.168.8.30"));
}

TEST (CorePeers, APlaceOnThisMachineIsToldAsTheCoreHost)
{
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");
    rig.clear();
    rig.motionHello();
    rig.stemdeckHello ("127.0.0.2");
    EXPECT_EQ (startingWith (rig.to ("prodj@192.168.8.60"), "/core/device/at"),
               (std::vector<std::string> { "/core/device/at s:motion s:" + coreHost, "/core/device/at s:stemdeck s:" + coreHost }));
}

TEST (CorePeers, MotionsPlaceIsToldOnlyFromAMotionHeard)
{
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");   // no Motion heard: the default target is no place
    EXPECT_TRUE (startingWith (rig.to ("prodj@192.168.8.60"), "/core/device/at").empty());
    rig.hello (rig.remoteMotion, "motion");
    rig.at (45.0);
    rig.hello (deskAt (60), "prodj");
    rig.at (61.0);
    rig.clear();
    rig.tick();   // back to the rig's own: that moves Core's target, not a Motion known to run
    EXPECT_TRUE (startingWith (rig.to ("prodj@192.168.8.60"), "/core/device/at").empty());
}

TEST (CorePeers, StemdecksPlaceIsToldOnlyFromAStemdeckHeard)
{
    CoreRig rig;
    rig.stemdeckHello ("192.168.8.30");
    rig.at (61.0);
    // The joining app is told before its own hello notices StemDeck's silence (B1's order).
    rig.hello (deskAt (60), "prodj");
    EXPECT_TRUE (has (rig.to ("prodj@192.168.8.60"), "/core/device/at s:stemdeck s:192.168.8.30"));
    EXPECT_TRUE (has (rig.journal(), "stemdeck: silent for a minute, its buses are empty"));
    rig.clear();
    rig.osc (deskAt (60), "/state/recall");
    EXPECT_TRUE (startingWith (rig.to ("prodj@192.168.8.60"), "/core/device/at").empty());
}

//==============================================================================
// test_core_prodj_wiring.Live

TEST (CorePeers, CoreStartsWithItOffAndSaysNothing)
{
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.clear();
    for (double at : { 1.0, 5.5, 10.6 })
    {
        rig.at (at);
        rig.tick();
    }
    EXPECT_TRUE (liveTargets (rig, "").empty());
}

TEST (CorePeers, ASwitchIsSaidInTheJournal)
{
    CoreRig rig;
    prodjJoinsAndSwitchesLiveOn (rig);
    EXPECT_TRUE (has (rig.journal(), "prodj: LIVE on (by prodj@192.168.8.60)"));
    rig.osc (rig.motion, "/prodj/live", { pyInt (0) });
    EXPECT_TRUE (has (rig.journal(), "prodj: LIVE off (by motion)"));
}

TEST (CorePeers, AnnounceLiveNeverBroadcasts)
{
    CoreRig rig;
    rig.motionHello();
    rig.hello (deskAt (40), "desk");
    rig.stemdeckHello ("127.0.0.2");
    rig.hello (deskAt (60), "prodj");
    rig.clear();
    rig.osc (deskAt (60), "/prodj/live", { pyInt (1) });
    EXPECT_FALSE (has (rig.to ("mixer"), "/prodj/live i:1"));
    EXPECT_EQ (liveTargets (rig, ""),
               (std::vector<std::string> { "desk@192.168.8.40 192.168.8.40:7773", "motion 127.0.0.1:7771",
                                           "prodj@192.168.8.60 192.168.8.60:7773", "stemdeck 127.0.0.2:7780" }));
}

TEST (CorePeers, LiveIsRepeatedOnTheTickWhileOn)
{
    CoreRig rig;
    prodjJoinsAndSwitchesLiveOn (rig);
    rig.clear();
    rig.at (4.9);
    rig.tick();
    EXPECT_TRUE (liveTargets (rig, "").empty());
    rig.at (5.1);
    rig.tick();
    EXPECT_EQ (liveTargets (rig, ""), (std::vector<std::string> { "motion 127.0.0.1:7771", "prodj@192.168.8.60 192.168.8.60:7773" }));
    rig.osc (deskAt (60), "/prodj/live", { pyInt (0) });
    rig.clear();
    rig.at (20.0);
    rig.tick();
    EXPECT_TRUE (liveTargets (rig, "").empty());
}

TEST (CorePeers, DueIsAskedOncePerTick)
{
    CoreRig rig;
    prodjJoinsAndSwitchesLiveOn (rig);
    rig.clear();
    rig.at (12.0);   // more than two periods late: still one repeat
    rig.tick();
    EXPECT_EQ (count (rig.to ("motion"), "/prodj/live i:1"), 1);
}

TEST (CorePeers, LiveIsSwitchedOnlyByWhoMay)
{
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.osc (deskAt (40), "/prodj/live", { pyInt (1) });   // on only while a pro-DJ app is followed
    EXPECT_TRUE (startingWith (rig.journal(), "prodj: LIVE").empty());
    rig.hello (deskAt (60), "prodj");
    rig.osc (rig.motion, "/prodj/live", { pyInt (1) });   // Motion only ever off
    rig.osc (rig.stranger, "/prodj/live", { pyInt (1) });
    EXPECT_TRUE (startingWith (rig.journal(), "prodj: LIVE").empty());
    rig.osc (deskAt (60), "/prodj/live", { pyStr ("abc") });
    EXPECT_EQ (rig.unknownCount ("/prodj/live"), 1);
    rig.osc (deskAt (40), "/prodj/live", { pyStr ("1") });
    EXPECT_TRUE (has (rig.journal(), "prodj: LIVE on (by desk@192.168.8.40)"));
    rig.osc (rig.motion, "/prodj/live", { pyFloat (0.0) });
    EXPECT_TRUE (has (rig.journal(), "prodj: LIVE off (by motion)"));
}

TEST (CorePeers, MotionsHelloIsAnsweredWithLive)
{
    CoreRig rig;
    rig.clear();
    rig.motionHello();
    EXPECT_EQ (rig.to ("motion"), (std::vector<std::string> { "/prodj/live i:0" }));
}

TEST (CorePeers, AKnownDeskSayingHelloAgainIsAnsweredWithLive)
{
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.clear();
    rig.hello (deskAt (40), "desk");
    EXPECT_EQ (rig.sendLines(), (std::vector<std::string> { "desk@192.168.8.40 /prodj/live i:0" }));
}

TEST (CorePeers, AKindChangeIsALeaveAndAJoin)
{
    CoreRig rig;
    rig.hello (deskAt (40), "desk");
    rig.clear();
    rig.hello (deskAt (40), "prodj");
    EXPECT_TRUE (has (rig.journal(), "desk: 192.168.8.40 now says prodj, joins again as prodj@192.168.8.40"));
    EXPECT_TRUE (has (rig.journal(), "desk: 192.168.8.40 joined, 1 of 4; replayed 19 messages"));
    EXPECT_TRUE (rig.to ("desk@192.168.8.40").empty());
    EXPECT_FALSE (rig.to ("prodj@192.168.8.40").empty());
    rig.osc (deskAt (40), "/prodj/live", { pyInt (1) });
    rig.clear();
    rig.osc (deskAt (40), "/channel/1/gain", { pyFloat (0.5) });   // a sender named by its new kind is left out
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/gain f:0.5"));
    EXPECT_TRUE (rig.to ("prodj@192.168.8.40").empty());
}

TEST (CorePeers, TheLastProdjLeavingSwitchesLiveOff)
{
    CoreRig rig;
    rig.motionHello();
    prodjJoinsAndSwitchesLiveOn (rig);
    rig.at (30.0);
    rig.hello (deskAt (40), "desk");
    rig.motionHello();
    rig.clear();
    rig.at (60.5);
    rig.tick();
    const auto lines = rig.journal();
    EXPECT_TRUE (has (lines, "desk: 192.168.8.60 silent for a minute, no longer followed"));
    EXPECT_TRUE (has (lines, "prodj: LIVE off, no pro-DJ app left"));
    EXPECT_EQ (liveTargets (rig, ""), (std::vector<std::string> { "desk@192.168.8.40 192.168.8.40:7773", "motion 127.0.0.1:7771" }));
    EXPECT_EQ (startingWith (rig.to ("motion"), "/prodj/live"), (std::vector<std::string> { "/prodj/live i:0" }));
}

TEST (CorePeers, RefusedPointersAreSeenByTheTrafficTable)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.hello (deskAt (60), "prodj");
    rig.clear();
    rig.osc (deskAt (60), "/stemdeck/pointer", { pyInt (0), pyFloat (0.5), pyFloat (0.5) });
    EXPECT_TRUE (rig.seenIn ("/stemdeck/pointer"));
    EXPECT_EQ (rig.sends(), 0u);
    EXPECT_TRUE (rig.journal().empty());
}

//==============================================================================
// test_core_prodj_wiring.Pointers

TEST (CorePeers, PointersAreForwardedOnlyThroughTheGate)
{
    CoreRig rig;
    rig.motionHello();
    rig.stemdeckHello ("127.0.0.2");
    rig.hello (deskAt (40), "desk");
    prodjJoinsAndSwitchesLiveOn (rig);
    rig.clear();
    rig.osc (deskAt (60), "/stemdeck/pointer", { pyInt (0), pyFloat (0.25), pyFloat (0.75) });
    rig.osc (deskAt (60), "/motion/pointer", { pyInt (1), pyFloat (0.5), pyFloat (0.5) });
    rig.osc (deskAt (40), "/motion/pointer", { pyInt (2), pyFloat (0.5), pyFloat (0.5) });   // not a pro-DJ app
    EXPECT_EQ (rig.sendLines(), (std::vector<std::string> { "stemdeck /stemdeck/pointer i:0 f:0.25 f:0.75",
                                                            "motion /motion/pointer i:1 f:0.5 f:0.5" }));
}

TEST (CorePeers, TheHandlersAreMappedFromTheTruth)
{
    CoreRig rig;
    rig.osc (deskAt (60), "/prodj/live", { pyInt (0) });
    rig.osc (deskAt (60), "/stemdeck/pointer", { pyInt (0), pyFloat (0.5), pyFloat (0.5) });
    rig.osc (deskAt (60), "/motion/pointer", { pyInt (0), pyFloat (0.5), pyFloat (0.5) });
    EXPECT_EQ (rig.unknownRows(), 0u);
    EXPECT_TRUE (rig.seenIn ("/prodj/live"));
    EXPECT_TRUE (rig.seenIn ("/stemdeck/pointer"));
    EXPECT_TRUE (rig.seenIn ("/motion/pointer"));
}

TEST (CorePeers, TheStemdeckFamilyHandlerLeavesPointersAlone)
{
    // /stemdeck/pointer matches the family's /stemdeck/* too: handled once, by the pointer's handler.
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    prodjJoinsAndSwitchesLiveOn (rig);
    rig.clear();
    rig.osc (deskAt (60), "/stemdeck/pointer", { pyInt (0), pyFloat (0.5), pyFloat (0.5) });
    EXPECT_EQ (rig.sendLines(), (std::vector<std::string> { "stemdeck /stemdeck/pointer i:0 f:0.5 f:0.5" }));
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CorePeers, ValuesRefusedAsksTheLiveGate)
{
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");
    rig.clear();
    rig.osc (deskAt (60), "/channel/1/gain", { pyFloat (0.5) });
    EXPECT_EQ (rig.sends(), 0u);
    rig.osc (deskAt (60), "/prodj/live", { pyInt (1) });
    rig.clear();
    rig.osc (deskAt (60), "/channel/1/gain", { pyFloat (0.5) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/gain f:0.5"));
}

//==============================================================================
// test_core_prodj_wiring.TheLiveGate

TEST (CorePeers, ARefusedValueIsSeenAsAnArrival)
{
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");
    rig.osc (deskAt (60), "/master/volume", { pyFloat (0.4) });
    EXPECT_TRUE (rig.seenIn ("/master/volume"));
    EXPECT_EQ (rig.peerOf ("/master/volume"), "prodj@192.168.8.60");
}

TEST (CorePeers, EveryDeskHelloLeavesItsKindSlotOrNot)
{
    // A pro-DJ app on another truth gets no slot; the kind it said still holds it back.
    CoreRig rig;
    rig.hello (deskAt (61), "prodj", otherHash);
    rig.clear();
    rig.osc (deskAt (61), "/channel/1/cue", { pyFloat (1.0) });
    EXPECT_EQ (rig.sends(), 0u);
    EXPECT_TRUE (rig.seenIn ("/channel/1/cue"));
}

TEST (CorePeers, EveryDeskWordFamilyHandlerAsksTheGate)
{
    CoreRig rig;
    rig.hello (deskAt (60), "prodj");
    rig.clear();
    for (const auto* address : { "/channel/1/cue", "/master/volume", "/filter/mode", "/aux-return/stem/push" })
        rig.osc (deskAt (60), address, { pyFloat (1.0) });
    EXPECT_EQ (rig.sends(), 0u);
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CorePeers, ThePointerGateReadsTheKindSaidToo)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    prodjJoinsAndSwitchesLiveOn (rig);
    rig.hello (deskAt (61), "prodj", otherHash);   // said prodj, no slot
    rig.hello (deskAt (62), "desk", otherHash);    // said desk, no slot
    rig.clear();
    rig.osc (deskAt (61), "/stemdeck/pointer", { pyInt (0), pyFloat (0.5), pyFloat (0.5) });
    rig.osc (deskAt (62), "/stemdeck/pointer", { pyInt (1), pyFloat (0.5), pyFloat (0.5) });
    EXPECT_EQ (rig.sendLines(), (std::vector<std::string> { "stemdeck /stemdeck/pointer i:0 f:0.5 f:0.5" }));
}

//==============================================================================
// test_core_vu_relay.CoreRunsIt

TEST (CorePeers, OnAThreadOfItsOwnAtTheTruthsPort)
{
    // Core's part of the relay: what reaches core.vu-relay goes on unread, byte for byte (the
    // service binds the truth's port and runs it on a thread of its own, Task 20).
    CoreRig rig;
    EXPECT_EQ (rig.truth.endpoint ("core", "vu-relay").port, 9003);
    rig.hello (deskAt (40), "desk");
    rig.clear();
    const auto bytes = stemMeterBundle();
    rig.vu (bytes);
    ASSERT_EQ (rig.net.transcript.size(), 1);
    EXPECT_EQ (rig.net.transcript[0]["raw"].toString(), juce::Base64::toBase64 (bytes.data(), bytes.size()));
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CorePeers, TowardsTheMotionItFollows)
{
    CoreRig rig;
    rig.motionHello();
    rig.vu (meterBundle());
    EXPECT_TRUE (rig.targetsOf ("vu").empty());   // the rig's own gets them from the analyzer
    rig.at (61.0);
    rig.tick();
    rig.hello (rig.remoteMotion, "motion");
    rig.clear();
    rig.vu (meterBundle());
    EXPECT_EQ (rig.targetsOf ("vu"), (std::vector<std::string> { "192.168.8.20:7772" }));
    rig.at (122.0);
    rig.tick();
    rig.clear();
    rig.vu (meterBundle());
    EXPECT_TRUE (rig.targetsOf ("vu").empty());
}

//==============================================================================
// test_core_deck_ports.DeckPortsInCore

namespace
{
void putDeckOnChannel (CoreRig& rig, int deck, int channel)
{
    rig.osc (rig.desk, "/channel/" + std::to_string (channel) + "/stem/turn", { pyStr (std::to_string (deck)) });
    rig.osc (rig.desk, "/channel/" + std::to_string (channel) + "/stem/push", { pyStr ("1") });
}

const Endpoint deckB { "127.0.0.3", 6001 };
}

TEST (CorePeers, DeckB3dGoesTo7781)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.deckHello (2, deckB);
    rig.osc (rig.desk, "/channel/3/3d", { pyStr ("0.4") });
    putDeckOnChannel (rig, 2, 3);
    EXPECT_TRUE (has (rig.to ("stemdeck-B"), "/stemdeck/2/3d f:0.4"));
    rig.osc (rig.desk, "/channel/3/3d", { pyStr ("0.9") });
    EXPECT_EQ (rig.to ("stemdeck-B").back(), "/stemdeck/2/3d f:0.9");
    for (const auto& target : rig.targetsOf ("stemdeck-B"))
        EXPECT_EQ (target, "127.0.0.3:7781");
}

TEST (CorePeers, DeckDHelloIsHeardAndIts3dGoesTo7783)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.deckHello (4, { "127.0.0.5", 6004 });
    EXPECT_TRUE (has (rig.to ("mixer"), "/stemdeck/4/present i:1"));
    rig.osc (rig.desk, "/channel/2/3d", { pyStr ("0.6") });
    putDeckOnChannel (rig, 4, 2);
    EXPECT_TRUE (has (rig.to ("stemdeck-D"), "/stemdeck/4/3d f:0.6"));
    EXPECT_EQ (rig.targetsOf ("stemdeck-D").back(), "127.0.0.5:7783");
}

TEST (CorePeers, ChannelSource4IsAcceptedAnd5IsRefused)
{
    {
        CoreRig rig ({ { "evening.json", R"({"values": {}, "channel.source": [4, 0, 0, 0]})" } });
        rig.engineHello ("boot-1");
        EXPECT_TRUE (has (rig.to ("engine"), "/channel/1/source i:4"));
    }
    CoreRig rig ({ { "evening.json", R"({"values": {}, "channel.source": [5, 0, 0, 0]})" } });
    rig.engineHello ("boot-1");
    EXPECT_TRUE (has (rig.to ("engine"), "/channel/1/source i:0"));
    EXPECT_FALSE (has (rig.to ("engine"), "/channel/1/source i:5"));
}

TEST (CorePeers, DeckAStaysOnTheBasePort)
{
    CoreRig rig;
    rig.deckHello (1, rig.deckA);
    EXPECT_EQ (rig.targetsOf ("stemdeck-A"), (std::vector<std::string> { "127.0.0.2:7780" }));
}

TEST (CorePeers, ThePortFollowsTheTruthsBase)
{
    juce::String error;
    auto data = parseJson (juce::JSON::toString (b0::truth().data()), error);
    for (auto& listener : *data["listeners"].getArray())
        if (listener["name"].toString() == "stemdeck.osc")
            listener.getDynamicObject()->setProperty ("port", 9100);
    const Truth moved (data);
    CoreRig::Options options;
    options.truth = &moved;
    CoreRig rig (CoreRig::WithOptions {}, options);
    rig.deckHello (1, rig.deckA);
    rig.deckHello (2, deckB);
    EXPECT_EQ (rig.targetsOf ("stemdeck-A"), (std::vector<std::string> { "127.0.0.2:9100" }));
    EXPECT_EQ (rig.targetsOf ("stemdeck-B"), (std::vector<std::string> { "127.0.0.3:9101" }));
}

TEST (CorePeers, TheHelloReplyGoesToTheDecksPort)
{
    CoreRig rig;
    rig.deckHello (2, deckB);
    EXPECT_EQ (rig.to ("stemdeck-B"), (std::vector<std::string> { "/stemdeck/2/3d f:1" }));
    EXPECT_EQ (rig.targetsOf ("stemdeck-B"), (std::vector<std::string> { "127.0.0.3:7781" }));
}

TEST (CorePeers, TheSelectorArrivalGoesToTheDecksPort)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.deckHello (2, deckB);
    rig.osc (rig.desk, "/channel/1/3d", { pyStr ("0.5") });
    rig.clear();
    putDeckOnChannel (rig, 2, 1);   // the deck arrives on a channel with a 3D: it is told it
    EXPECT_EQ (rig.to ("stemdeck-B"), (std::vector<std::string> { "/stemdeck/2/3d f:0.5" }));
    for (const auto& target : rig.targetsOf ("stemdeck-B"))
        EXPECT_EQ (target, "127.0.0.3:7781");
}

TEST (CorePeers, TheLeaveNeutralGoesToTheDecksPort)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.deckHello (2, deckB);
    putDeckOnChannel (rig, 2, 1);
    rig.clear();
    rig.osc (rig.desk, "/channel/1/stem/turn", { pyStr ("-2") });   // back to ANALOG
    rig.osc (rig.desk, "/channel/1/stem/push", { pyStr ("1") });
    EXPECT_EQ (rig.to ("stemdeck-B"), (std::vector<std::string> { "/stemdeck/2/3d f:1" }));
    EXPECT_EQ (rig.targetsOf ("stemdeck-B"), (std::vector<std::string> { "127.0.0.3:7781" }));
}

TEST (CorePeers, BothDecksOnOneHost3dGoesToTheirOwnPorts)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.deckHello (1, rig.deckA);
    rig.deckHello (2, { "127.0.0.2", 6002 });
    rig.osc (rig.desk, "/channel/1/3d", { pyStr ("0.2") });
    rig.osc (rig.desk, "/channel/2/3d", { pyStr ("0.3") });
    rig.clear();
    putDeckOnChannel (rig, 1, 1);
    putDeckOnChannel (rig, 2, 2);
    EXPECT_EQ (rig.to ("stemdeck-A"), (std::vector<std::string> { "/stemdeck/1/3d f:0.2" }));
    EXPECT_EQ (rig.to ("stemdeck-B"), (std::vector<std::string> { "/stemdeck/2/3d f:0.3" }));
    EXPECT_EQ (rig.targetsOf ("stemdeck-A"), (std::vector<std::string> { "127.0.0.2:7780" }));
    EXPECT_EQ (rig.targetsOf ("stemdeck-B"), (std::vector<std::string> { "127.0.0.2:7781" }));
}

TEST (CorePeers, EngineModeProdjLiveReachesEveryDeckOnce)
{
    // StemDeck as a whole listens on the base port, which is deck A's too: never twice to one socket.
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.deckHello (1, rig.deckA);
    rig.deckHello (2, { "127.0.0.2", 6002 });
    rig.clear();
    prodjJoinsAndSwitchesLiveOn (rig);
    EXPECT_EQ (liveTargets (rig, "stemdeck"), (std::vector<std::string> { "stemdeck 127.0.0.2:7780", "stemdeck-B 127.0.0.2:7781" }));
}

namespace
{
// The recorded B0 truth with the one word it predates, taken from the package's truth.
Truth b0WithDeckAudio()
{
    juce::String error;
    auto data = parseJson (juce::JSON::toString (b0::truth().data()), error);
    const auto packaged = std::get<Truth> (Truth::load (juce::File (A3_PACKAGE_TRUTH), std::nullopt)).data();
    data["addresses"].getDynamicObject()->setProperty ("engine.deck-audio", packaged["addresses"]["engine.deck-audio"]);
    return Truth (data);
}
}

TEST (CorePeers, EngineDeckAudioReachesTheDeckSelector)
{
    const auto truth = b0WithDeckAudio();
    CoreRig::Options options;
    options.truth = &truth;
    CoreRig rig (CoreRig::WithOptions {}, options);
    rig.stemdeckHello ("127.0.0.2");
    rig.deckHello (1, rig.deckA);
    rig.clear();
    rig.osc (rig.engine, "/engine/deck/1/audio", { pyInt (0) });
    EXPECT_EQ (startingWith (rig.to ("motion"), "/stemdeck/1/present"), (std::vector<std::string> { "/stemdeck/1/present i:0" }));
    rig.clear();
    rig.osc (rig.engine, "/engine/deck/1/audio", { pyInt (1) });
    EXPECT_EQ (startingWith (rig.to ("motion"), "/stemdeck/1/present"), (std::vector<std::string> { "/stemdeck/1/present i:1" }));
}

namespace
{
size_t linesAbout (const std::vector<std::string>& journal, const std::string& word)
{
    return static_cast<size_t> (std::count_if (journal.begin(), journal.end(),
                                               [&] (const std::string& line) { return line.find (word) != std::string::npos; }));
}
}

TEST (CorePeers, ATruthWithoutTheDeckAudioWordIsSaidOnceAtStart)
{
    CoreRig rig;   // the recorded B0 truth
    EXPECT_EQ (linesAbout (rig.journal (true), "engine.deck-audio"), 1u);
    EXPECT_EQ (linesAbout (rig.journal (true), "hellos alone"), 1u);
}

TEST (CorePeers, ATruthWithTheDeckAudioWordSaysNothingAboutIt)
{
    const auto truth = b0WithDeckAudio();
    CoreRig::Options options;
    options.truth = &truth;
    CoreRig rig (CoreRig::WithOptions {}, options);
    EXPECT_EQ (linesAbout (rig.journal (true), "engine.deck-audio"), 0u);
}

TEST (CorePeers, ThePackagesTruthMapsTheEngineDeckAudioWord)
{
    const auto truth = std::get<Truth> (Truth::load (juce::File (A3_PACKAGE_TRUTH), std::nullopt));
    CoreRig::Options options;
    options.truth = &truth;
    CoreRig rig (CoreRig::WithOptions {}, options);
    rig.stemdeckHello ("127.0.0.2");
    rig.deckHello (1, rig.deckA);
    rig.clear();
    rig.osc (rig.engine, "/engine/deck/1/audio", { pyInt (0) });
    EXPECT_EQ (startingWith (rig.to ("motion"), "/stemdeck/1/present"), (std::vector<std::string> { "/stemdeck/1/present i:0" }));
}
