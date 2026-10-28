// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <algorithm>
#include "AppTruth.h"
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
