// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The router's values, flags, relay, the engine's words and the deck ring, driven through a3core::Core
// on a fake network. Ports of B1's (a3-core 7a7b2441f74e) test_core_channel_chain,
// test_core_desk_first_wiring, test_core_hello_gate_wiring, test_core_engine_mode_in_process, the
// engine-mode cases of test_core_engine_mode_wiring and test_core_stems_wiring, and
// test_core_evening.TheEngineActuallyDoesIt. Where the Python test read a3-core.py's source, the port
// holds the behaviour that source text was there for.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "CoreRig.h"

using namespace corerig;

namespace
{
std::vector<std::string> channelWordsCoreHears (const Truth& truth)
{
    std::vector<std::string> keys;
    const auto data = truth.data();   // a copy: the loop must not run over a temporary's properties
    for (const auto& entry : data["addresses"].getDynamicObject()->getProperties())
    {
        const auto key = entry.name.toString().toStdString();
        if (key.rfind ("channel.", 0) != 0)
            continue;
        if (const auto* to = entry.value["to"].getArray(); to != nullptr && to->contains ("core"))
            keys.push_back (key);
    }
    return keys;
}

const std::string thirtyEvening = R"({"values": {"/channel/1/gain": 0.3}, "channel.source": [0, 0, 0, 0]})";
}

//==============================================================================
// test_core_channel_chain.OneChainOneElse

TEST (CoreValues, EveryChannelWordCoreHearsHasItsBranch)
{
    CoreRig rig;
    const auto keys = channelWordsCoreHears (rig.truth);
    ASSERT_GE (keys.size(), 12u);
    for (const auto& key : keys)
        rig.osc (rig.desk, rig.truth.address (key, { { "ch", 2 } }), { pyFloat (0.0) });
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CoreValues, TheParameterIsDecidedInOneChain)
{
    // A value one branch served is not also written down as unknown (a fader fell into the last
    // chain's `else` until 2026-10-01); a word no branch serves is, once.
    CoreRig rig;
    rig.osc (rig.desk, "/channel/1/volume", { pyStr ("0.5") });
    rig.osc (rig.desk, "/channel/1/gain", { pyStr ("0.5") });
    rig.osc (rig.desk, "/channel/1/cue", { pyStr ("1") });
    EXPECT_EQ (rig.unknownRows(), 0u);
    rig.osc (rig.desk, "/channel/1/cue/led", { pyFloat (1.0) });
    EXPECT_EQ (rig.unknownCount ("/channel/1/cue/led"), 1);
    EXPECT_EQ (rig.unknownRows(), 1u);
}

//==============================================================================
// test_core_desk_first_wiring.TheReplayCarriesTheDesk

TEST (CoreValues, EveryRelayedValueIsHeardWithItsSender)
{
    // Heard before the repeat is dropped: a desk value equal to what was last passed on is still the desk's.
    CoreRig rig ({ { "evening.json", thirtyEvening } });
    rig.motionHello();
    rig.osc (rig.motion, "/channel/1/gain", { pyFloat (0.5) });
    rig.osc (rig.desk, "/channel/1/gain", { pyStr ("0.5") });
    EXPECT_EQ (count (rig.to ("mixer"), "/channel/1/gain f:0.5"), 1);
    EXPECT_EQ (count (rig.to ("motion"), "/channel/1/gain f:0.5"), 0);
    rig.clear();
    rig.engineHello ("boot-1");
    ASSERT_FALSE (rig.to ("self").empty());
    EXPECT_EQ (rig.to ("self").front(), "/channel/1/gain f:0.5");
}

TEST (CoreValues, TheReplayLaysTheDeskOverTheEvening)
{
    CoreRig rig ({ { "evening.json", R"({"values": {"/channel/1/gain": 0.3, "/channel/2/gain": 0.4}})" } });
    rig.motionHello();
    rig.osc (rig.desk, "/channel/1/gain", { pyStr ("0.9") });
    rig.osc (rig.desk, "/channel/4/volume", { pyStr ("0.1") });
    rig.osc (rig.motion, "/channel/2/gain", { pyFloat (0.6) });   // not the desk's: the evening's value is replayed
    rig.clear();
    rig.engineHello ("boot-1");
    EXPECT_EQ (rig.to ("self"), (std::vector<std::string> { "/channel/1/gain f:0.9", "/channel/2/gain f:0.4", "/channel/4/volume f:0.1",
                                                             "/state/recall i:1000002" }));
    rig.clear();
    rig.engineHello ("boot-2");   // every replay of the run carries the desk
    EXPECT_EQ (rig.to ("self").front(), "/channel/1/gain f:0.9");
}

//==============================================================================
// test_core_evening.TheEngineActuallyDoesIt

TEST (CoreValues, EveryValuePassedOnIsWrittenDown)
{
    // Both doors -- relay() for the desk's and Motion's values, broadcast() for Core's own -- write the
    // evening once it is Core's own (#56: relay noted without writing).
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.at (0.5);
    rig.osc (rig.desk, "/channel/2/gain", { pyStr ("0.45") });
    rig.osc (rig.desk, "/channel/2/cue", { pyStr ("1") });
    rig.at (1.0);
    rig.tick();   // the decks' first report is passed on too, and moves the evening once more
    rig.at (3.0);
    rig.tick();
    const auto values = rig.json ("evening.json")["values"];
    EXPECT_EQ (static_cast<double> (values["/channel/2/gain"]), 0.45);
    EXPECT_EQ (static_cast<double> (values["/channel/2/cue/led"]), 1.0);
    EXPECT_EQ (static_cast<double> (values["/channel/2/cue"]), 1.0);
}

TEST (CoreValues, A3CoreWritesAndReplays)
{
    juce::String written;
    {
        CoreRig rig;
        rig.engineHello ("boot-1");
        rig.osc (rig.desk, "/master/volume", { pyStr ("0.25") });
        rig.stop();
        written = rig.file ("evening.json").loadFileAsString();
    }
    ASSERT_TRUE (written.contains ("\"/master/volume\": 0.25"));
    CoreRig next ({ { "evening.json", written.toStdString() } });
    next.engineHello ("boot-1");
    EXPECT_TRUE (has (next.to ("self"), "/master/volume f:0.25"));
    EXPECT_TRUE (has (next.to ("engine"), "/master/volume f:0.25"));
}

//==============================================================================
// test_core_hello_gate_wiring.TheHandlersAsk

TEST (CoreValues, EveryValueHandlerRefusesBeforeItDoesAnything)
{
    CoreRig rig;
    rig.clear();
    for (const auto* address : { "/channel/1/gain", "/master/volume", "/filter/frequency" })
    {
        EXPECT_NO_THROW (rig.osc (rig.stranger, address, { pyStr ("not a number") }));
        rig.osc (rig.stranger, address, { pyFloat (0.5) });
        EXPECT_FALSE (rig.seenIn (address)) << address;
    }
    EXPECT_EQ (rig.sends(), 0u);
    rig.stop();
    EXPECT_FALSE (rig.file ("state.json").exists());
}

TEST (CoreValues, OnlyRelayedValuesAreGated)
{
    // Buttons and turns are edges, not state: they pass a host that has not said hello.
    CoreRig rig;
    rig.clear();
    rig.osc (rig.stranger, "/channel/1/gain", { pyFloat (0.5) });
    EXPECT_EQ (rig.sends(), 0u);
    rig.osc (rig.stranger, "/channel/1/cue", { pyFloat (1.0) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/cue/led f:1"));
    rig.osc (rig.stranger, "/channel/2/stem/turn", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/2/source/cursor i:1"));
    rig.osc (rig.stranger, "/filter/mode", { pyFloat (1.0) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/filter/led s:high_pass"));
}

TEST (CoreValues, TheFirstRefusalIsSaidOnce)
{
    CoreRig rig;
    rig.osc (rig.stranger, "/channel/1/gain", { pyFloat (0.5) });
    rig.osc (rig.stranger, "/master/volume", { pyFloat (0.5) });
    rig.osc ({ "192.168.8.51", 9999 }, "/master/volume", { pyFloat (0.5) });
    const auto lines = rig.journal();
    EXPECT_EQ (count (lines, "192.168.8.50 (192.168.8.50): values before its hello ignored"), 1);
    EXPECT_EQ (count (lines, "192.168.8.51 (192.168.8.51): values before its hello ignored"), 1);
}

TEST (CoreValues, ADifferingTruthIsSaidOnce)
{
    CoreRig rig;
    rig.motionHello (otherHash);
    rig.clear();
    rig.osc (rig.motion, "/channel/1/gain", { pyFloat (0.5) });
    rig.osc (rig.motion, "/channel/1/gain", { pyFloat (0.6) });
    EXPECT_EQ (rig.journal(), (std::vector<std::string> { "motion (127.0.0.1): values ignored, its a3-osc.json differs from Core's" }));
    EXPECT_EQ (rig.sends(), 0u);
}

TEST (CoreValues, AHelloOpensTheGateAfterTheReplay)
{
    CoreRig rig;
    rig.osc (rig.motion, "/channel/1/gain", { pyFloat (0.5) });
    EXPECT_FALSE (has (rig.to ("mixer"), "/channel/1/gain f:0.5"));
    rig.clear();
    rig.motionHello();
    EXPECT_EQ (rig.to ("motion"), (std::vector<std::string> { "/prodj/live i:0" }));
    rig.osc (rig.motion, "/channel/1/gain", { pyFloat (0.5) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/gain f:0.5"));
}

TEST (CoreValues, CoresOwnReplayComesFromATrustedSocket)
{
    CoreRig rig ({ { "evening.json", thirtyEvening } });
    rig.osc ({ loopbackAddress(), 40002 }, "/channel/1/gain", { pyFloat (0.7) });   // loopback, but not Core's own socket
    EXPECT_FALSE (has (rig.to ("mixer"), "/channel/1/gain f:0.7"));
    rig.engineHello ("boot-1");
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/gain f:0.3"));
    EXPECT_TRUE (has (rig.to ("engine"), "/channel/1/gain f:0.3"));
}

TEST (CoreValues, TheDeskIsTrustedByHostAfterTheArguments)
{
    // --mixer can point the desk elsewhere: the host trusted is the one the arguments name.
    CoreRig rig ({}, Endpoint { "192.168.8.99", 7772 });
    rig.osc ({ "192.168.8.99", 50001 }, "/channel/1/gain", { pyStr ("0.5") });
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/1/gain f:0.5"));
    rig.osc ({ "192.168.8.11", 50001 }, "/channel/2/gain", { pyStr ("0.5") });
    EXPECT_FALSE (has (rig.to ("motion"), "/channel/2/gain f:0.5"));
}

TEST (CoreValues, TheHelloOpensOnlyOnCoresOwnFingerprint)
{
    CoreRig rig;
    const Endpoint radla { "192.168.8.50", 9999 };
    rig.osc (radla, "/device/hello", { pyStr ("radla"), pyStr (otherHash) });
    rig.osc (radla, "/channel/1/gain", { pyFloat (0.5) });
    EXPECT_FALSE (has (rig.to ("motion"), "/channel/1/gain f:0.5"));
    rig.osc (radla, "/device/hello", { pyStr ("radla"), pyStr (ownHash()) });
    rig.osc (radla, "/channel/1/gain", { pyFloat (0.5) });
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/1/gain f:0.5"));
}

//==============================================================================
// test_core_stems_wiring (engine mode)

// P47: the FX Return has no mode and no cursor any more; Core speaks the channels' stems only.
TEST (CoreValues, RecallAndStartSpeakTheStems)
{
    CoreRig rig;
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/stem/cursor i:8"));
    EXPECT_TRUE (startingWith (rig.to ("mixer"), "/aux-return/stem").empty());
    rig.clear();
    rig.osc (rig.motion, "/state/recall", { pyInt (1) });   // said whole, though nothing changed
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/stem/cursor i:8"));
    EXPECT_TRUE (startingWith (rig.to ("motion"), "/aux-return/stem").empty());
}

TEST (CoreValues, TheChannelTurnHasItsBranch)
{
    CoreRig rig;
    rig.clear();
    rig.osc (rig.desk, "/channel/2/stem/turn", { pyStr ("1") });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/2/source/cursor i:1"));
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CoreValues, TheReturnFamilyIsMapped)
{
    CoreRig rig;
    rig.clear();
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/aux-return/cue/led f:1"));
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CoreValues, TheChannelCueHasItsBranch)
{
    CoreRig rig;
    rig.clear();
    rig.osc (rig.desk, "/channel/1/cue", { pyStr ("1") });
    EXPECT_EQ (rig.to ("motion"), (std::vector<std::string> { "/channel/1/cue/led f:1", "/channel/1/cue f:1" }));
}

TEST (CoreValues, AReportIsAnnounced)
{
    CoreRig rig;
    rig.clear();
    rig.osc (rig.deckA, "/stemdeck/1/1/buses", { pyInt (16) });
    // The first announcement not at start-up passes on every word (nothing was noted as passed on yet).
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/stem/cursor i:8"));
    EXPECT_TRUE (startingWith (rig.to ("mixer"), "/aux-return/stem").empty());
}

TEST (CoreValues, AReportOfTheWrongTypeChangesNothing)
{
    // Stems::report takes ints: the router refuses every other tag first, as B1's Stems.report refuses
    // a float, a bool, a text or None (test_core_stems' test_a_damaged_report_changes_nothing).
    CoreRig rig;
    rig.clear();
    const std::vector<OscArg> damaged { pyFloat (16.0), pyBool (true), pyBool (false), pyStr ("16"), OscArg { 'N', std::monostate {} },
                                        pyInt (64), pyInt (-1) };
    for (const auto& value : damaged)
        rig.osc (rig.deckA, "/stemdeck/1/2/buses", { value });
    rig.osc (rig.deckA, "/stemdeck/1/2/buses");
    rig.osc (rig.deckA, "/stemdeck/3/1/buses", { pyInt (16) });   // deck 3, stem 1 is pair 9: no such pair
    EXPECT_EQ (rig.sends(), 0u);
    EXPECT_EQ (rig.unknownCount ("/stemdeck/1/2/buses"), 8);
    EXPECT_EQ (rig.unknownCount ("/stemdeck/3/1/buses"), 1);
    rig.osc (rig.deckA, "/stemdeck/1/2/buses", { OscArg { 'h', int64_t { 16 } } });
    EXPECT_GT (rig.sends(), 0u);
}

TEST (CoreValues, AReturningStemdeckIsAskedForEverything)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    EXPECT_EQ (rig.to ("stemdeck"), (std::vector<std::string> { "/stemdeck/recall i:1", "/prodj/live i:0" }));
    rig.at (30.0);
    rig.stemdeckHello ("127.0.0.2");
    EXPECT_EQ (rig.to ("stemdeck").size(), 2u);
    rig.at (100.0);
    rig.stemdeckHello ("127.0.0.2");   // back after a silence: it may have restarted with other switches
    EXPECT_EQ (rig.to ("stemdeck").size(), 4u);
}

TEST (CoreValues, AStemdeckHelloIsHeardBeforeTheSilenceCheck)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.at (100.0);
    rig.stemdeckHello ("127.0.0.2");
    EXPECT_FALSE (has (rig.journal(), "stemdeck: silent for a minute, its buses are empty"));
}

TEST (CoreValues, EveryHelloChecksForSilence)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.at (100.0);
    rig.motionHello();
    EXPECT_EQ (count (rig.journal(), "stemdeck: silent for a minute, its buses are empty"), 1);
}

TEST (CoreValues, SilenceUnmutesAnalogAndShowsA)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.osc (rig.deckA, "/stemdeck/1/1/buses", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/stem i:1"));
    rig.clear();
    rig.at (100.0);
    rig.motionHello();
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/stem i:0"));
}

TEST (CoreValues, SilenceLeavesTheChannelSourcesAlone)
{
    CoreRig rig ({ { "state.json", R"({"stems": {"sources": [1, 0, 1, null]}})" } });
    rig.stemdeckHello ("127.0.0.2");
    rig.at (100.0);
    rig.motionHello();
    rig.osc (rig.desk, "/master/phones-mix", { pyStr ("0.5") });
    rig.stop();
    const auto sources = rig.json ("state.json")["stems"]["sources"];
    EXPECT_EQ (dumpPython (sources), "[1, 0, 1, null]");
}

TEST (CoreValues, TheHelloAndTheSilenceAskTheWatch)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.stemdeckHello ("192.168.8.70");
    EXPECT_TRUE (has (rig.journal(), "stemdeck: a second StemDeck at 192.168.8.70 while following 127.0.0.2 -- only one may run; "
                                     "still following 127.0.0.2"));
    EXPECT_EQ (rig.to ("stemdeck").size(), 2u);
    for (const auto& entry : rig.net.transcript)
        EXPECT_NE (entry["host"].toString(), "192.168.8.70");
}

TEST (CoreValues, TheStemdeckFamilyIsMapped)
{
    CoreRig rig;
    rig.osc (rig.deckA, "/stemdeck/1/1/buses", { pyInt (16) });
    EXPECT_EQ (rig.unknownRows(), 0u);
    EXPECT_TRUE (rig.seenIn ("/stemdeck/1/1/buses"));
    rig.osc (rig.deckA, "/stemdeck/recall", { pyInt (1) });   // a word Core sends, never one it serves
    EXPECT_EQ (rig.unknownCount ("/stemdeck/recall"), 1);
}

TEST (CoreValues, TheMasterHandlerRemembers)
{
    CoreRig rig;
    rig.osc (rig.desk, "/master/phones-mix", { pyStr ("0.75") });
    rig.at (2.5);
    rig.tick();
    EXPECT_EQ (static_cast<double> (rig.json ("state.json")["phones_mix"]), 0.75);
}

// P47: the FX Return's push is its cue button; there is no cursor to move first.
TEST (CoreValues, APushOnCueTogglesIt)
{
    CoreRig rig;
    rig.clear();
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/aux-return/cue/led f:1"));
}

TEST (CoreValues, AToggleFlipsLightsAndSounds)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.clear();
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    EXPECT_EQ (startingWith (rig.to ("motion"), "/aux-return/cue/led"),
               (std::vector<std::string> { "/aux-return/cue/led f:1", "/aux-return/cue/led f:0" }));
    EXPECT_EQ (rig.to ("engine"), (std::vector<std::string> { "/aux-return/cue f:1", "/aux-return/cue f:0" }));
}

TEST (CoreValues, TheReturnHandlerRemembers)
{
    CoreRig rig;
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    rig.stop();
    EXPECT_TRUE (static_cast<bool> (rig.json ("state.json")["return_cue"]));
}

// P47: the FX Return's encoder turn does nothing; B0's truth still names it, so it reaches the
// return's handler and is noted as unknown, like the lamp Core alone may say.
TEST (CoreValues, TheLastElseNotesItAsUnknown)
{
    CoreRig rig;
    rig.osc (rig.desk, "/aux-return/stem/turn", { pyInt (1) });
    rig.osc (rig.desk, "/aux-return/cue/led", { pyFloat (1.0) });
    EXPECT_EQ (rig.unknownCount ("/aux-return/stem/turn"), 1);
    EXPECT_EQ (rig.unknownCount ("/aux-return/cue/led"), 1);
    EXPECT_EQ (rig.unknownRows(), 2u);
}

TEST (CoreValues, APushIsSaidWithOrWithoutStemdeck)
{
    CoreRig rig;
    rig.osc (rig.desk, "/channel/2/stem/turn", { pyInt (1) });
    rig.clear();
    rig.osc (rig.desk, "/channel/2/stem/push", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/2/source i:1"));
}

TEST (CoreValues, APushLoads)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.osc (rig.desk, "/channel/2/stem/turn", { pyInt (1) });
    rig.clear();
    rig.osc (rig.desk, "/channel/2/stem/push", { pyInt (1) });
    EXPECT_EQ (rig.to ("engine"), (std::vector<std::string> { "/channel/2/source i:1" }));
}

// P47: the FX Return's push is its cue alone; it no longer tidies the stems, with or without StemDeck.
TEST (CoreValues, AReturnPushLeavesTheStemsAlone)
{
    for (const bool withStemdeck : { false, true })
    {
        CoreRig rig;
        if (withStemdeck)
            rig.stemdeckHello ("127.0.0.2");
        rig.osc (rig.deckA, "/stemdeck/1/1/buses", { pyInt (0b0110) });   // stem 1 on buses 2 and 3: against the rules
        rig.clear();
        rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
        EXPECT_TRUE (startingWith (rig.to ("mixer"), "/channel/").empty()) << withStemdeck;
        EXPECT_EQ (rig.to ("stemdeck").size(), 0u);
    }
}

TEST (CoreValues, APushWithoutStemdeckSendsNothing)
{
    CoreRig rig;
    rig.osc (rig.desk, "/channel/1/stem/turn", { pyInt (1) });
    rig.osc (rig.desk, "/channel/1/stem/push", { pyInt (1) });
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    for (const auto& entry : rig.net.transcript)
        EXPECT_FALSE (entry["to"].toString().startsWith ("stemdeck"));
}

TEST (CoreValues, ATurnOnlySelects)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.clear();
    rig.osc (rig.desk, "/channel/2/stem/turn", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/2/source/cursor i:1"));
    EXPECT_FALSE (has (rig.to ("mixer"), "/channel/2/source i:1"));
    EXPECT_TRUE (rig.to ("engine").empty());
}

TEST (CoreValues, TheReturnPushOnlyCues)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.clear();
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    EXPECT_TRUE (startingWith (rig.to ("mixer"), "/aux-return/stem").empty());
    EXPECT_EQ (rig.to ("engine"), (std::vector<std::string> { "/aux-return/cue f:1" }));
}

TEST (CoreValues, AStateWithoutStemsStartsOnA)
{
    CoreRig rig ({ { "state.json", R"({"stems": "garbage"})" } });
    for (int channel = 1; channel <= 4; ++channel)
        EXPECT_TRUE (has (rig.to ("mixer"), "/channel/" + std::to_string (channel) + "/stem/cursor i:8"));
}

TEST (CoreValues, TheChannelSourcesSurviveTheRoundTrip)
{
    CoreRig rig ({ { "state.json", R"({"stems": {"sources": [1, 0, 1, 0]}})" } });
    rig.osc (rig.desk, "/master/phones-mix", { pyStr ("0.5") });
    rig.stop();
    EXPECT_EQ (dumpPython (rig.json ("state.json")["stems"]["sources"]), "[1, 0, 1, 0]");
}

// P47: the return's cursor and mode are gone from the state; the channels' cursors stay.
TEST (CoreValues, TheCursorsAreWrittenDown)
{
    CoreRig rig;
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    rig.stop();
    const auto stems = rig.json ("state.json")["stems"];
    EXPECT_FALSE (stems.hasProperty ("return_mode"));
    EXPECT_FALSE (stems.hasProperty ("return_cursor"));
    EXPECT_EQ (dumpPython (stems["cursors"]), "[8, 8, 8, 8]");
}

TEST (CoreValues, TheCursorsSurviveTheRoundTrip)
{
    CoreRig rig ({ { "state.json", R"({"stems": {"cursors": [8, 8, 8, 7]}})" } });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/4/stem/cursor i:7"));
}

//==============================================================================
// test_core_engine_mode_in_process.EngineModeInProcess

TEST (CoreValues, NothingReachesTheEngineBeforeItsHelloEngineModeInProcess)
{
    CoreRig rig;
    rig.osc (rig.desk, "/channel/1/gain", { pyFloat (0.5) });
    rig.osc (rig.desk, "/master/volume", { pyFloat (0.7) });
    EXPECT_TRUE (rig.to ("engine").empty());
}

TEST (CoreValues, TheHelloReplaysThenSendsTheToken)
{
    CoreRig rig ({ { "evening.json", thirtyEvening } });
    rig.engineHello ("boot-1");
    const auto self = rig.to ("self");
    ASSERT_EQ (self.size(), 2u);
    EXPECT_EQ (self.front(), "/channel/1/gain f:0.3");
    EXPECT_EQ (self.back(), "/state/recall i:1000002");
    EXPECT_TRUE (has (rig.journal(), "engine: hello from boot-1, replaying the evening"));
}

TEST (CoreValues, TheSameBootIdAgainReplaysNothing)
{
    CoreRig rig ({ { "evening.json", thirtyEvening } });
    rig.engineHello ("boot-1");
    const auto told = rig.to ("self").size();
    rig.engineHello ("boot-1");
    EXPECT_EQ (rig.to ("self").size(), told);
}

TEST (CoreValues, AfterTheHelloValuesReachTheEngine)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.clear();
    rig.osc (rig.desk, "/channel/1/gain", { pyFloat (0.6) });
    rig.osc (rig.desk, "/channel/2/azimuth", { pyFloat (200.0) });
    EXPECT_EQ (rig.to ("engine"), (std::vector<std::string> { "/channel/1/gain f:0.6", "/channel/2/azimuth f:180" }));
}

TEST (CoreValues, TheTokenRecallSendsTheStateThenOpens)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    const auto engine = rig.to ("engine");
    ASSERT_FALSE (engine.empty());
    EXPECT_TRUE (startingWith (engine, "/aux-return/source").empty());
    EXPECT_TRUE (has (engine, "/aux-return/cue f:0"));
    EXPECT_TRUE (has (engine, "/channel/1/cue f:0"));
    EXPECT_EQ (engine.back(), "/engine/open i:1");
}

TEST (CoreValues, ADeviceRecallDoesNotOpen)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.clear();
    rig.osc (rig.motion, "/state/recall", { pyInt (1) });
    EXPECT_FALSE (has (rig.to ("engine"), "/engine/open i:1"));
}

TEST (CoreValues, NoStemBusCommandReachesStemdeck)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.osc (rig.deckA, "/stemdeck/1/1/buses", { pyInt (0b0110) });
    rig.at (1.0);
    rig.tick();
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    EXPECT_EQ (rig.to ("stemdeck"), (std::vector<std::string> { "/stemdeck/recall i:1", "/prodj/live i:0" }));
}

TEST (CoreValues, ADeckOnAChannel)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.deckHello (1, rig.deckA);
    rig.osc (rig.desk, "/channel/3/3d", { pyFloat (0.4) });
    rig.clear();
    rig.osc (rig.desk, "/channel/3/stem/turn", { pyInt (1) });
    rig.osc (rig.desk, "/channel/3/stem/push", { pyInt (1) });
    EXPECT_EQ (rig.to ("engine"), (std::vector<std::string> { "/channel/3/source i:1" }));
    EXPECT_EQ (rig.to ("stemdeck-A"), (std::vector<std::string> { "/stemdeck/1/3d f:0.4" }));
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/3/source i:1"));

    rig.clear();
    rig.osc (rig.desk, "/channel/3/3d", { pyFloat (0.9) });
    rig.osc (rig.desk, "/channel/3/azimuth", { pyFloat (10.0) });
    EXPECT_TRUE (rig.to ("engine").empty());
    EXPECT_EQ (rig.to ("stemdeck-A"), (std::vector<std::string> { "/stemdeck/1/3d f:0.9" }));
}

namespace
{
void putDeckOnChannel3 (CoreRig& rig)
{
    rig.engineHello ("boot-1");
    rig.deckHello (1, rig.deckA);
    rig.osc (rig.desk, "/channel/3/azimuth", { pyFloat (40.0) });
    rig.osc (rig.desk, "/channel/3/elevation", { pyFloat (-10.0) });
    rig.osc (rig.desk, "/channel/3/filter/frequency", { pyFloat (0.25) });
    rig.osc (rig.desk, "/channel/3/filter/q", { pyFloat (0.75) });
    rig.clear();
    rig.osc (rig.desk, "/channel/3/stem/turn", { pyInt (1) });
    rig.osc (rig.desk, "/channel/3/stem/push", { pyInt (1) });
}
}

TEST (CoreValues, AChannelBackOnAnalogGetsItsCurrent3dFilterAndPosition)
{
    CoreRig rig;
    putDeckOnChannel3 (rig);
    rig.osc (rig.desk, "/channel/3/3d", { pyFloat (0.0) });   // the desk's 3D while on the deck
    rig.osc (rig.desk, "/channel/3/filter/frequency", { pyFloat (0.5) });
    rig.clear();
    rig.osc (rig.desk, "/channel/3/stem/turn", { pyInt (-1) });   // cursor to analog
    rig.osc (rig.desk, "/channel/3/stem/push", { pyInt (1) });
    EXPECT_EQ (rig.to ("engine"), (std::vector<std::string> { "/channel/3/source i:0", "/channel/3/3d f:0", "/channel/3/filter/frequency f:0.5",
                                                               "/channel/3/filter/q f:0.75", "/channel/3/azimuth f:40",
                                                               "/channel/3/elevation f:-10" }));
}

TEST (CoreValues, ADeckArrivingSendsTheEngineNoAnalogState)
{
    CoreRig rig;
    putDeckOnChannel3 (rig);
    EXPECT_EQ (rig.to ("engine"), (std::vector<std::string> { "/channel/3/source i:1" }));
}

TEST (CoreValues, ATurnReportsTheCursorAndAPushTheSource)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.clear();
    rig.osc (rig.desk, "/channel/2/stem/turn", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/2/source/cursor i:1"));
    EXPECT_FALSE (has (rig.to ("motion"), "/channel/2/source i:1"));
    EXPECT_TRUE (rig.to ("engine").empty());
    rig.clear();
    rig.osc (rig.desk, "/channel/2/stem/push", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/2/source i:1"));
    EXPECT_FALSE (has (rig.to ("motion"), "/channel/2/source/cursor i:1"));   // the cursor already stood there
    rig.osc (rig.desk, "/channel/3/stem/turn", { pyInt (1) });
    rig.osc (rig.desk, "/channel/3/stem/push", { pyInt (1) });   // deck A moves to channel 3: channel 2 falls back
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/2/source/cursor i:0"));
    rig.clear();
    rig.osc (rig.motion, "/state/recall", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/3/source/cursor i:1"));
    EXPECT_TRUE (has (rig.to ("motion"), "/channel/1/source/cursor i:0"));
}

//==============================================================================
// test_core_engine_mode_wiring (engine mode)

TEST (CoreValues, TheProjectIsAnEngineSessionInEngineMode)
{
    // Before the first replay the evening is the file's.
    CoreRig rig ({ { "evening.json", thirtyEvening } });
    rig.engineHello ("boot-1");
    EXPECT_EQ (rig.to ("self").front(), "/channel/1/gain f:0.3");
}

TEST (CoreValues, TheLiveEveningIsCoresOwn)
{
    // After the first replay the evening is what Core itself passed on, never the file's again.
    CoreRig rig ({ { "evening.json", thirtyEvening } });
    rig.engineHello ("boot-1");
    rig.motionHello();
    rig.osc (rig.motion, "/channel/1/gain", { pyFloat (0.7) });
    rig.clear();
    rig.engineHello ("boot-2");
    EXPECT_EQ (rig.to ("self").front(), "/channel/1/gain f:0.7");
}

TEST (CoreValues, EverySendPointHasItsEngineBranch)
{
    // channel, master, filter, the stems' sources and the return cue each reach the engine (the beat's
    // tempo is osc_handler_beat's, Task 19).
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.clear();
    rig.osc (rig.desk, "/channel/1/volume", { pyStr ("0.5") });
    rig.osc (rig.desk, "/master/booth", { pyStr ("0.5") });
    rig.osc (rig.desk, "/filter/resonance", { pyStr ("0.5") });
    rig.osc (rig.desk, "/channel/1/stem/turn", { pyInt (1) });
    rig.osc (rig.desk, "/channel/1/stem/push", { pyInt (1) });
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    EXPECT_EQ (rig.to ("engine"), (std::vector<std::string> { "/channel/1/volume f:0.5", "/master/booth f:0.5", "/filter/resonance f:0.5",
                                                               "/channel/1/source i:1", "/aux-return/cue f:1" }));
}

TEST (CoreValues, ChannelValuesGoAfterTheRelayThroughTheDeckFilter)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.deckHello (1, rig.deckA);
    rig.osc (rig.desk, "/channel/3/stem/turn", { pyInt (1) });
    rig.osc (rig.desk, "/channel/3/stem/push", { pyInt (1) });
    rig.clear();
    rig.osc (rig.desk, "/channel/3/filter/q", { pyStr ("0.3") });
    rig.osc (rig.desk, "/channel/3/gain", { pyStr ("0.3") });
    std::vector<std::string> order;
    for (const auto& entry : rig.net.transcript)
        order.push_back (entry["to"].toString().toStdString() + " " + entry["address"].toString().toStdString());
    EXPECT_EQ (order, (std::vector<std::string> { "motion /channel/3/filter/q", "motion /channel/3/gain", "engine /channel/3/gain" }));
}

TEST (CoreValues, FlagsAndPositionsReachTheEngine)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.clear();
    rig.osc (rig.desk, "/channel/1/cue", { pyStr ("1") });
    rig.osc (rig.desk, "/channel/2/filter", { pyStr ("1") });
    rig.osc (rig.desk, "/channel/3/azimuth", { pyStr ("-190") });
    rig.osc (rig.desk, "/channel/4/elevation", { pyStr ("95") });
    EXPECT_EQ (rig.to ("engine"), (std::vector<std::string> { "/channel/1/cue f:1", "/channel/2/filter f:1", "/channel/3/azimuth f:-180",
                                                               "/channel/4/elevation f:90" }));
}

TEST (CoreValues, NothingReachesTheEngineBeforeItsHelloEverySendPointHasItsEngineBranch)
{
    CoreRig rig;
    rig.osc (rig.desk, "/channel/1/cue", { pyStr ("1") });
    rig.osc (rig.desk, "/channel/3/azimuth", { pyStr ("10") });
    rig.osc (rig.desk, "/filter/mode", { pyStr ("high_pass") });
    rig.osc (rig.desk, "/channel/1/stem/turn", { pyInt (1) });
    rig.osc (rig.desk, "/channel/1/stem/push", { pyInt (1) });
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    EXPECT_TRUE (rig.to ("engine").empty());
}

TEST (CoreValues, TheEngineClientIsTheTruths)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    const auto where = rig.truth.endpoint ("engine", "osc");
    size_t told = 0;
    for (const auto& entry : rig.net.transcript)
        if (entry["to"].toString() == "engine")
        {
            ++told;
            EXPECT_EQ (entry["host"].toString().toStdString(), where.host);
            EXPECT_EQ (static_cast<int> (entry["port"]), where.port);
        }
    EXPECT_GT (told, 0u);
}

TEST (CoreValues, TheOtherWords)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.clear();
    rig.osc (rig.desk, "/master/phones-mix", { pyStr ("0.25") });
    rig.osc (rig.desk, "/filter/frequency", { pyStr ("0.75") });
    rig.osc (rig.desk, "/filter/mode", { pyStr ("high_pass") });
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    EXPECT_EQ (rig.to ("engine"), (std::vector<std::string> { "/master/phones-mix f:0.25", "/filter/frequency f:0.75", "/filter/mode f:1",
                                                               "/aux-return/cue f:1" }));
}

TEST (CoreValues, EngineHelloIsMapped)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    EXPECT_TRUE (rig.seenIn ("/engine/hello"));
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CoreValues, TheEngineRecallSendsStateThenOpenAndNoReaperGate)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    const auto engine = rig.to ("engine");
    ASSERT_GE (engine.size(), 2u);
    EXPECT_EQ (engine.back(), "/engine/open i:1");
    EXPECT_TRUE (startingWith (engine, "/aux-return/source").empty());
    for (const auto& entry : rig.net.transcript)
        EXPECT_NE (entry["to"].toString(), "reaper");
}

TEST (CoreValues, TheHelloGateIsNotAsked)
{
    // As device.hello: a hello is not a value, so a host the gate has never heard may say it.
    CoreRig rig ({ { "evening.json", thirtyEvening } });
    rig.osc ({ "192.168.8.77", 41000 }, "/engine/hello", { pyStr ("boot-1") });
    EXPECT_TRUE (has (rig.to ("self"), "/channel/1/gain f:0.3"));
}

TEST (CoreValues, TheHelloReplaysThenRecallsWithTheToken)
{
    CoreRig rig ({ { "evening.json", thirtyEvening } });
    rig.engineHello ("boot-1");
    std::vector<std::string> order;
    for (const auto& entry : rig.net.transcript)
    {
        if (entry.hasProperty ("journal"))
            order.push_back (entry["journal"].toString().toStdString());
        else if (entry["to"].toString() == "self")
            order.push_back ("self " + entry["address"].toString().toStdString());
    }
    const std::vector<std::string> expected { "engine: hello from boot-1, replaying the evening", "self /channel/1/gain",
                                              "startup: replayed 1 values from " + rig.file ("evening.json").getFullPathName().toStdString(),
                                              "self /state/recall" };
    ASSERT_GE (order.size(), 6u);
    EXPECT_EQ (std::vector<std::string> (order.end() - 5, order.end() - 1), expected);
    rig.stop();
    EXPECT_TRUE (rig.file ("evening.json").loadFileAsString().contains ("\"/channel/1/gain\": 0.30000001192092896"));
}

TEST (CoreValues, DeckHelloIsMappedAndResendsThreeD)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.deckHello (1, rig.deckA);
    EXPECT_EQ (rig.to ("stemdeck-A"), (std::vector<std::string> { "/stemdeck/1/3d f:1" }));
    EXPECT_TRUE (has (rig.journal(), "stemdeck: deck 1 at 127.0.0.2"));
    rig.osc (rig.desk, "/channel/3/3d", { pyStr ("0.4") });
    rig.osc (rig.desk, "/channel/3/stem/turn", { pyInt (1) });
    rig.osc (rig.desk, "/channel/3/stem/push", { pyInt (1) });
    rig.clear();
    rig.deckHello (1, rig.deckA);
    EXPECT_EQ (rig.to ("stemdeck-A"), (std::vector<std::string> { "/stemdeck/1/3d f:0.4" }));
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CoreValues, DeckWordsGoToTheDecksOwnHost)
{
    CoreRig rig;
    rig.deckHello (2, { "192.168.8.80", 6001 });
    ASSERT_EQ (rig.net.transcript.size() > 0, true);
    bool found = false;
    for (const auto& entry : rig.net.transcript)
        if (entry["to"].toString() == "stemdeck-B")
        {
            found = true;
            EXPECT_EQ (entry["host"].toString(), "192.168.8.80");
            EXPECT_EQ (static_cast<int> (entry["port"]), rig.truth.port ("stemdeck", "osc") + 1);
        }
    EXPECT_TRUE (found);
}

TEST (CoreValues, EngineModeSelectorIsTheDeckSelector)
{
    // The desk's ring words move the deck selector, not the stem mirror's cursors.
    CoreRig rig;
    rig.clear();
    rig.osc (rig.desk, "/channel/1/stem/turn", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/1/source/cursor i:1"));
    EXPECT_TRUE (startingWith (rig.to ("mixer"), "/channel/1/stem/cursor").empty());
    rig.stop();
    EXPECT_EQ (dumpPython (rig.json ("state.json")["stems"]["cursors"]), "[8, 8, 8, 8]");
}

TEST (CoreValues, SourcesAndPresenceAreReported)
{
    CoreRig rig;
    rig.deckHello (1, rig.deckA);
    EXPECT_TRUE (has (rig.to ("mixer"), "/stemdeck/1/present i:1"));
    rig.clear();
    rig.at (10.0);
    rig.tick();
    EXPECT_EQ (rig.to ("mixer"), (std::vector<std::string> { "/stemdeck/1/present i:0" }));
}

TEST (CoreValues, TheDecksExistInEngineModeOnly)
{
    // The decks' places come from the evening and go back into it, beside its values.
    CoreRig rig ({ { "evening.json", R"({"values": {}, "channel.source": [0, 2, 0, 0]})" } });
    EXPECT_TRUE (has (rig.to ("mixer"), "/channel/2/source i:2"));
    rig.engineHello ("boot-1");
    rig.stop();
    EXPECT_EQ (dumpPython (rig.json ("evening.json")["channel.source"]), "[0, 2, 0, 0]");
}

TEST (CoreValues, ThreeDOnADeckChannelGoesToTheDeck)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.deckHello (1, rig.deckA);
    rig.osc (rig.desk, "/channel/3/stem/turn", { pyInt (1) });
    rig.osc (rig.desk, "/channel/3/stem/push", { pyInt (1) });
    rig.clear();
    rig.osc (rig.desk, "/channel/3/3d", { pyStr ("0.6") });
    EXPECT_EQ (rig.to ("stemdeck-A"), (std::vector<std::string> { "/stemdeck/1/3d f:0.6" }));
    EXPECT_TRUE (rig.to ("engine").empty());
    rig.stop();
    EXPECT_EQ (static_cast<double> (rig.json ("state.json")["channels"][2]["three_d"]), 0.6);
}

// P47: an older state's return mode and cursor are ignored; no source reaches the engine.
TEST (CoreValues, AnOldReturnModeIsIgnored)
{
    CoreRig rig ({ { "state.json", R"({"stems": {"return_mode": 0, "return_cursor": 0}})" } });
    rig.engineHello ("boot-1");
    EXPECT_TRUE (startingWith (rig.to ("engine"), "/aux-return/source").empty());
    EXPECT_TRUE (startingWith (rig.to ("mixer"), "/aux-return/stem").empty());
}

//==============================================================================
// Carried from the earlier reviews (Task 17): a list passed on, a NaN kept.

TEST (CoreValues, ASteadyNanDoesNotPutTheStateFileOff)
{
    // B1 compares the state it offers with the last by Python's ==, and a container holding the same
    // NaN object twice is equal: a NaN 3D held does not restart the debounce on every later message.
    CoreRig rig;
    rig.motionHello();
    rig.at (1.0);
    rig.osc (rig.motion, "/channel/3/3d", { pyFloat (std::nan ("")) });
    for (const double at : { 1.5, 2.0, 2.5 })
    {
        rig.at (at);
        rig.osc (rig.motion, "/channel/2/gain", { pyFloat (at / 10.0) });
    }
    rig.at (3.05);
    rig.tick();
    ASSERT_TRUE (rig.file ("state.json").exists());
    EXPECT_TRUE (rig.file ("state.json").loadFileAsString().contains ("\"three_d\": NaN"));
}

TEST (CoreValues, AFreshNanIsAChange)
{
    // A NaN that arrives anew is another object in B1, so the state differs and the debounce restarts.
    CoreRig rig;
    rig.motionHello();
    rig.at (1.0);
    rig.osc (rig.motion, "/channel/3/3d", { pyFloat (std::nan ("")) });
    rig.at (2.5);
    rig.osc (rig.motion, "/channel/3/3d", { pyFloat (std::nan ("")) });
    rig.at (3.05);
    rig.tick();
    EXPECT_FALSE (rig.file ("state.json").exists());
    rig.at (4.6);
    rig.tick();
    EXPECT_TRUE (rig.file ("state.json").exists());
}
