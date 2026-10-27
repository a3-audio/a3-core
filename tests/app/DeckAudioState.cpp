// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "DeckAudioReport.h"
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
