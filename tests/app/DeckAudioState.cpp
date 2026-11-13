// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <algorithm>
#include "AppTruth.h"
#include "ConfigEdits.h"
#include "DeckAudioReport.h"
#include "EngineFixture.h"
#include "a3-links/LinkPlan.h"

namespace
{
using Bits = std::vector<bool>;

a3app::IoMap twoDecks()
{
    a3app::IoMap map;
    map.jack["deckA_1"] = { "AES67 deck A:out_1" };
    map.jack["deckA_aux_1"] = { "AES67 deck A:out_9" };
    map.jack["deckB_1"] = { "AES67 deck B:out_1" };
    map.jack["deckB_aux_1"] = { "AES67 deck B:out_9" };
    map.jack["main_top_1"] = { "system:playback_1" };
    return map;
}

// What reached Core: the hello by its address, a deck's state as "address value".
std::vector<std::string> said (const std::vector<juce::OSCMessage>& messages)
{
    std::vector<std::string> lines;
    for (const auto& m : messages)
    {
        auto line = m.getAddressPattern().toString().toStdString();
        if (m.size() == 1 && m[0].isInt32())
            line += " " + std::to_string (m[0].getInt32());
        lines.push_back (line);
    }
    return lines;
}
}

TEST (DeckAudioState, AllLinksLiveIsOne)
{
    EXPECT_EQ (a3app::deckAudioLinked (twoDecks(), 2, {}), (Bits { true, true }));
}

TEST (DeckAudioState, OneMissingPortIsZero)
{
    EXPECT_EQ (a3app::deckAudioLinked (twoDecks(), 2, { "AES67 deck B:out_9" }), (Bits { true, false }))
        << "only the slot whose link lacks its peer";
    EXPECT_EQ (a3app::deckAudioLinked (twoDecks(), 2, { "system:playback_1" }), (Bits { true, true }))
        << "a peer of another port does not count against a deck";
}

TEST (DeckAudioState, SlotWithoutMapEntriesIsZero)
{
    EXPECT_EQ (a3app::deckAudioLinked (twoDecks(), 3, {}), (Bits { true, true, false }));
    EXPECT_EQ (a3app::deckAudioLinked ({}, 2, {}), (Bits { false, false }));
}

// A8: one entry per slot of the count, k = 1..N, whatever N is.
TEST (DeckAudioState, EverySlotOfTheCountIsReported)
{
    a3app::IoMap map;
    for (int slot = 1; slot <= 5; ++slot)
        a3app::setDeckSource (map, slot, slot == 2 ? a3app::DeckSource::aes67 : a3app::DeckSource::local);
    EXPECT_EQ (a3app::deckAudioLinked (map, 5, {}), (Bits { true, true, true, true, true }));
    // A deck in B-format mode has no master pair: its stereo peers are missing too.
    EXPECT_EQ (a3app::deckAudioLinked (map, 5, { "StemDeck-E:cue_9", "StemDeck-E:master_L", "StemDeck-E:master_R" }),
               (Bits { true, true, true, true, false }));
    EXPECT_EQ (a3app::deckAudioLinked (map, 6, { "StemDeck-C:aux_1", "StemDeck-C:master_L", "StemDeck-C:master_R" }),
               (Bits { true, true, false, true, true, false }));
}

// A peer's ports vanish (its host restarts) and come back: the plan holds the links again.
TEST (EngineStatus, ReturningPeerIsRelinked)
{
    const auto map = twoDecks();
    const std::vector<std::string> own { "deckA_1", "deckA_aux_1", "deckB_1", "deckB_aux_1", "main_top_1" };
    const std::vector<std::string> inputs { "deckA_1", "deckA_aux_1", "deckB_1", "deckB_aux_1" };
    std::vector<std::string> graph { "a3-engine:deckA_1", "a3-engine:deckA_aux_1", "a3-engine:deckB_1", "a3-engine:deckB_aux_1",
                                     "a3-engine:main_top_1", "system:playback_1", "AES67 deck A:out_1", "AES67 deck A:out_9" };

    const auto gone = a3::links::planConnections (map, {}, {}, graph, inputs);
    EXPECT_EQ (gone.missingPeers, (std::vector<std::string> { "AES67 deck B:out_1", "AES67 deck B:out_9" }));
    EXPECT_EQ (a3app::deckAudioLinked (map, 2, gone.missingPeers), (Bits { true, false }));

    graph.push_back ("AES67 deck B:out_1");
    graph.push_back ("AES67 deck B:out_9");
    const auto back = a3::links::planConnections (map, {}, {}, graph, inputs);
    EXPECT_TRUE (back.missingPeers.empty());
    EXPECT_NE (std::find (back.add.begin(), back.add.end(), a3::links::Link { "AES67 deck B:out_1", "a3-engine:deckB_1" }), back.add.end());
    EXPECT_EQ (a3app::deckAudioLinked (map, 2, back.missingPeers), (Bits { true, true }));
}

TEST (DeckAudioState, NothingIsReportedBeforeTheFirstLinkPlan)
{
    a3app::BackendStatus status;
    status.connecting = true;
    EXPECT_FALSE (a3app::deckAudioReport (status, twoDecks(), 2).has_value()) << "an empty missing list before the plan is not 'all live'";

    status.linksPlanned = true;
    EXPECT_EQ (a3app::deckAudioReport (status, twoDecks(), 2), (std::optional<Bits> { Bits { true, true } }));
    status.missingPeers = { "AES67 deck B:out_1" };
    EXPECT_EQ (a3app::deckAudioReport (status, twoDecks(), 2), (std::optional<Bits> { Bits { true, false } }));

    status.connecting = false;   // --no-connect: no links to report on
    EXPECT_FALSE (a3app::deckAudioReport (status, twoDecks(), 2).has_value());
}

// EngineRole's control tick ends with this step: the backend's link state reaches Core as
// /engine/deck/{k}/audio with every hello and at once on a change, and not before the first link plan.
TEST (DeckAudioReport, TheBackendsLinkStateReachesCoreWithTheHello)
{
    using Lines = std::vector<std::string>;
    std::vector<juce::OSCMessage> toCore;
    a3app::EngineHost host { a3test::engineConfig (4, 2), a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory),
                             a3apptest::engineTruth(), [&] (const juce::OSCMessage& m) { toCore.push_back (m); } };
    const auto map = twoDecks();
    a3app::BackendStatus status;
    status.connecting = true;

    a3app::reportDeckAudio (status, map, 2, host);
    host.controlTick (0.0);
    EXPECT_EQ (said (toCore), (Lines { "/engine/hello" })) << "no deck state before the first link plan";

    toCore.clear();
    status.linksPlanned = true;
    status.missingPeers = { "AES67 deck B:out_1" };
    a3app::reportDeckAudio (status, map, 2, host);
    EXPECT_EQ (said (toCore), (Lines { "/engine/deck/1/audio 1", "/engine/deck/2/audio 0" })) << "the first plan goes out at once";

    toCore.clear();
    a3app::reportDeckAudio (status, map, 2, host);
    host.controlTick (1.0);
    EXPECT_TRUE (toCore.empty()) << "no change and no hello due: nothing";
    host.controlTick (2.0);
    EXPECT_EQ (said (toCore), (Lines { "/engine/hello", "/engine/deck/1/audio 1", "/engine/deck/2/audio 0" })) << "every hello repeats the states";

    toCore.clear();
    status.missingPeers.clear();
    a3app::reportDeckAudio (status, map, 2, host);
    EXPECT_EQ (said (toCore), (Lines { "/engine/deck/2/audio 1" })) << "the deck whose peer came back, at once";
}

// Four slots reach Core as /engine/deck/1..4/audio, each its own state.
TEST (DeckAudioReport, FourSlotsReachCoreAsOneToFour)
{
    using Lines = std::vector<std::string>;
    std::vector<juce::OSCMessage> toCore;
    a3app::EngineHost host { a3test::engineConfig (4, 4), a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory),
                             a3apptest::engineTruth(), [&] (const juce::OSCMessage& m) { toCore.push_back (m); } };
    a3app::IoMap map;
    for (int slot = 1; slot <= 4; ++slot)
        a3app::setDeckSource (map, slot, a3app::DeckSource::local);
    a3app::BackendStatus status;
    status.connecting = true;
    status.linksPlanned = true;
    status.missingPeers = { "StemDeck-C:bformat_1", "StemDeck-C:master_L", "StemDeck-C:master_R" };   // B-format: no pair
    host.controlTick (0.0);
    toCore.clear();
    a3app::reportDeckAudio (status, map, 4, host);
    EXPECT_EQ (said (toCore), (Lines { "/engine/deck/1/audio 1", "/engine/deck/2/audio 1", "/engine/deck/3/audio 0",
                                       "/engine/deck/4/audio 1" }));
}

namespace
{
// The peers of a slot's B-format group (16 + aux 16 + cue 9) from a source.
std::vector<std::string> bformatPeers (int slot, a3app::DeckSource source)
{
    std::vector<std::string> peers;
    for (const auto& [port, peer] : a3app::deckLinks (slot, source))
        peers.push_back (peer);
    return peers;
}

std::vector<std::string> joined (std::vector<std::string> a, const std::vector<std::string>& b)
{
    a.insert (a.end(), b.begin(), b.end());
    return a;
}
}

// Links are the truth, not the signal: stereo only when the pair is linked and the B-format is not.
TEST (DeckAudioState, AStereoDeckIsOneWhosePairAloneIsLinked)
{
    using a3app::DeckMode;
    EXPECT_EQ (a3app::deckModeOf (false, true), DeckMode::stereo);
    EXPECT_EQ (a3app::deckModeOf (true, false), DeckMode::bformat);
    EXPECT_EQ (a3app::deckModeOf (true, true), DeckMode::bformat) << "both: B-format, as before the pair existed";
    EXPECT_EQ (a3app::deckModeOf (false, false), DeckMode::bformat) << "neither: unchanged";
}

// StemDeck in stereo mode has no B-format ports (a remote deck's 2-channel stream none of MAIN/AUX/CUE): the slot is
// present through its pair, and in stereo mode; without the pair as well it is absent.
TEST (DeckAudioState, AStereoDeckIsPresentAndTheModeFollowsTheGraph)
{
    using a3app::DeckMode;
    using Modes = std::vector<DeckMode>;
    a3app::IoMap map;
    a3app::setDeckSource (map, 1, a3app::DeckSource::local);
    a3app::setDeckSource (map, 2, a3app::DeckSource::aes67);
    a3app::setDeckSource (map, 3, a3app::DeckSource::local);
    const std::vector<std::string> stereoA { "StemDeck-A:master_L", "StemDeck-A:master_R" };
    const std::vector<std::string> stereoB { "AES67 deck B:receive_MASTER_L", "AES67 deck B:receive_MASTER_R" };

    // A in stereo (its B-format gone), B in B-format (its pair gone), C with both present.
    const auto missing = joined (bformatPeers (1, a3app::DeckSource::local), stereoB);
    EXPECT_EQ (a3app::deckModes (map, 3, missing), (Modes { DeckMode::stereo, DeckMode::bformat, DeckMode::bformat }));
    EXPECT_EQ (a3app::deckAudioLinked (map, 3, missing), (Bits { true, true, true }));

    // B goes stereo: its 2-channel stream has only the master pair.
    const auto bStereo = joined (bformatPeers (1, a3app::DeckSource::local), bformatPeers (2, a3app::DeckSource::aes67));
    EXPECT_EQ (a3app::deckModes (map, 3, bStereo), (Modes { DeckMode::stereo, DeckMode::stereo, DeckMode::bformat }));

    // A gone altogether: absent, and B-format (no stereo pair to play).
    const auto aGone = joined (missing, stereoA);
    EXPECT_EQ (a3app::deckAudioLinked (map, 3, aGone), (Bits { false, true, true }));
    EXPECT_EQ (a3app::deckModes (map, 3, aGone)[0], DeckMode::bformat);
    // Half a pair is no pair.
    EXPECT_EQ (a3app::deckModes (map, 3, joined (bformatPeers (1, a3app::DeckSource::local), { stereoA[1] }))[0],
               DeckMode::bformat);
}

// The control tick hands each slot's mode to the engine's controls, once the links are planned.
TEST (DeckAudioReport, TheModesReachTheEnginesControls)
{
    a3app::IoMap map;
    a3app::setDeckSource (map, 1, a3app::DeckSource::local);
    a3app::setDeckSource (map, 2, a3app::DeckSource::local);
    a3::EngineControls controls (4, 2);
    a3app::BackendStatus status;
    status.connecting = true;
    status.missingPeers.clear();
    for (const auto& peer : bformatPeers (1, a3app::DeckSource::local))
        status.missingPeers.push_back (peer);

    a3app::applyDeckModes (status, map, 2, controls);
    EXPECT_FALSE (controls.isDeckStereo (1)) << "nothing before the first link plan";

    status.linksPlanned = true;
    a3app::applyDeckModes (status, map, 2, controls);
    EXPECT_TRUE (controls.isDeckStereo (1));
    EXPECT_FALSE (controls.isDeckStereo (2));

    status.missingPeers.clear();   // A back in B-format: both groups present
    a3app::applyDeckModes (status, map, 2, controls);
    EXPECT_FALSE (controls.isDeckStereo (1));
}

// The same modes reach Core as /engine/deck/{k}/stereo, so Core treats a channel on a stereo deck as analog.
TEST (DeckAudioReport, TheModesReachCoreWithTheHello)
{
    using Lines = std::vector<std::string>;
    std::vector<juce::OSCMessage> toCore;
    a3app::EngineHost host { a3test::engineConfig (4, 2), a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory),
                             a3apptest::engineTruth(), [&] (const juce::OSCMessage& m) { toCore.push_back (m); } };
    a3app::IoMap map;
    a3app::setDeckSource (map, 1, a3app::DeckSource::local);
    a3app::setDeckSource (map, 2, a3app::DeckSource::local);
    a3app::BackendStatus status;
    status.connecting = true;
    for (const auto& peer : bformatPeers (1, a3app::DeckSource::local))   // A in stereo
        status.missingPeers.push_back (peer);
    host.controlTick (0.0);
    toCore.clear();

    a3app::reportDeckModes (status, map, 2, host);
    EXPECT_TRUE (toCore.empty()) << "nothing before the first link plan";

    status.linksPlanned = true;
    a3app::reportDeckModes (status, map, 2, host);
    EXPECT_EQ (said (toCore), (Lines { "/engine/deck/1/stereo 1", "/engine/deck/2/stereo 0" }));

    toCore.clear();
    status.missingPeers.clear();   // A back in B-format
    a3app::reportDeckModes (status, map, 2, host);
    EXPECT_EQ (said (toCore), (Lines { "/engine/deck/1/stereo 0" }));
}
