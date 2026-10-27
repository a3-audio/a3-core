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

TEST (StatusGrouping, DroppedTextCountsAndNamesEachLink)
{
    EXPECT_EQ (a3app::droppedText ({}), "none");
    EXPECT_EQ (a3app::droppedText ({ "deckA_cue_L -> x:y", "deckB_aux_R -> p:q" }),
               "2 retired links dropped:\ndeckA_cue_L -> x:y\ndeckB_aux_R -> p:q");
    EXPECT_EQ (a3app::droppedText ({ "deckA_cue_L -> x:y" }), "1 retired link dropped:\ndeckA_cue_L -> x:y");
}
