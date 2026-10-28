// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "StatusPage.h"

using Groups = std::vector<std::pair<std::string, int>>;

TEST (StatusGrouping, GroupsMissingPortsByClient)
{
    std::vector<std::string> missing;
    for (int i = 1; i <= 41; ++i)
        missing.push_back ("AES67 deck B:capture_" + std::to_string (i));
    missing.push_back ("StemDeck-A:out_1");
    missing.push_back ("StemDeck-A:out_2");
    missing.push_back ("system:capture_1");

    EXPECT_EQ (a3app::groupMissingByClient (missing),
               (Groups { { "AES67 deck B", 41 }, { "StemDeck-A", 2 }, { "system", 1 } }))
        << "one entry per client, in the order the clients first appear";
    EXPECT_TRUE (a3app::groupMissingByClient ({}).empty());
}

TEST (StatusGrouping, ClientIsEverythingBeforeTheFirstColon)
{
    EXPECT_EQ (a3app::groupMissingByClient ({ "a:b:c", "a:d", "nocolon" }), (Groups { { "a", 2 }, { "nocolon", 1 } }));
}

TEST (StatusGrouping, TextNamesEachClientWithItsCount)
{
    EXPECT_EQ (a3app::missingText ({}), "none");
    EXPECT_EQ (a3app::missingText ({ "AES67 deck B:in_1", "AES67 deck B:in_2", "system:capture_1" }),
               "AES67 deck B (2), system (1)");
}

TEST (StatusGrouping, DroppedTextCountsAndGroupsBySlot)
{
    EXPECT_EQ (a3app::droppedText ({}), "none");
    EXPECT_EQ (a3app::droppedText ({ "deckA_cue_L -> x:y" }), "1 retired link dropped:\ndeckA: cue_L");
    EXPECT_EQ (a3app::droppedText ({ "deckA_cue_L -> x:y", "deckB_aux_R -> p:q", "deckA_cue_R -> x:y", "mystery -> a:b" }),
               "4 retired links dropped:\ndeckA: cue_L cue_R\ndeckB: aux_R\nmystery -> a:b");
}

// P47: a retired config key is listed after the dropped links, on STATUS's DROPPED row.
TEST (StatusGrouping, TheIgnoredRetiredKeysFollowTheDroppedLinks)
{
    EXPECT_EQ (a3app::droppedText ({}, std::vector<std::string> {}), "none");
    EXPECT_EQ (a3app::droppedText ({}, { "engine.returnAnalogPair" }), "1 retired key ignored: engine.returnAnalogPair");
    EXPECT_EQ (a3app::droppedText ({ "deckA_cue_L -> x:y" }, { "engine.returnAnalogPair" }),
               "1 retired link dropped:\ndeckA: cue_L\n1 retired key ignored: engine.returnAnalogPair");
}

TEST (StatusGrouping, TheRetiredRigCaseFitsInFourLines)
{
    std::vector<std::string> eight;
    for (const char* deck : { "deckA", "deckB" })
        for (const char* kind : { "cue", "aux" })
            for (const char* side : { "L", "R" })
                eight.push_back (std::string (deck) + "_" + kind + "_" + side + " -> x:y");
    const auto text = a3app::droppedText (eight, 4);
    EXPECT_EQ (text, "8 retired links dropped:\ndeckA: cue_L cue_R aux_L aux_R\ndeckB: cue_L cue_R aux_L aux_R");
}

TEST (StatusGrouping, WhatDoesNotFitIsSaidNotCut)
{
    const std::vector<std::string> many { "deckA_cue_L -> a:b", "deckB_cue_L -> a:b", "deckC_cue_L -> a:b", "deckD_cue_L -> a:b" };
    const auto text = a3app::droppedText (many, 3);
    EXPECT_EQ (text, "4 retired links dropped:\ndeckA: cue_L\n+3 more");
    EXPECT_EQ (juce::StringArray::fromLines (text).size(), 3);
}
