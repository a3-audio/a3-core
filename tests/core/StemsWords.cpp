// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of the live part of a3-core's tools/tests/test_core_stems_reaper.py (B1 7a7b2441f74e): what
// the stem mirror becomes in engine mode -- the desk's announcements and the switch commands to
// StemDeck. The REAPER sends (analog_messages, stems_messages, return_source_messages) are not
// ported: the engine has no REAPER tracks (plan ruling P1).
#include <map>

#include <gtest/gtest.h>

#include "B0Truth.h"
#include "StemsWords.h"

using namespace a3core;

namespace
{
std::map<std::string, std::vector<OscArg>> said (const Stems& s)
{
    std::map<std::string, std::vector<OscArg>> out;
    for (const auto& message : announcements (s, b0::truth()))
        out[message.address] = message.args;
    return out;
}

std::vector<OscArg> ints (std::initializer_list<int> values)
{
    std::vector<OscArg> out;
    for (int value : values)
        out.push_back (pyInt (value));
    return out;
}
}

// WhatTheDeskHears

TEST (StemsWords, AChannelOnStemIsAnnouncedAsItsBusMask)
{
    Stems s;
    s.sources[0] = Stems::stemSource;
    s.report (1, 1);
    s.report (6, 1);
    EXPECT_EQ (said (s)["/channel/1/stem"], ints ({ 0b100001 }));
}

TEST (StemsWords, AnUndecidedChannelShowsItsBus)
{
    Stems s;
    s.report (3, 1);
    EXPECT_EQ (said (s)["/channel/1/stem"], ints ({ 0b100 }));
}

TEST (StemsWords, AChannelOnAnalogShowsAWhateverIsOnItsBus)
{
    // 0 = the analog input (a3-osc.json); the stem on bus 1 is unheard.
    Stems s;
    s.sources[0] = Stems::analogSource;
    s.report (3, 1);
    EXPECT_EQ (said (s)["/channel/1/stem"], ints ({ 0 }));
}

TEST (StemsWords, TheReturnIsCursorThenAuxFlags)
{
    Stems s;
    s.report (3, 1 << 4);   // AUX
    const auto args = said (s)["/aux-return/stem"];
    ASSERT_EQ (args.size(), 9u);
    EXPECT_EQ (std::vector<OscArg> (args.begin() + 1, args.end()), ints ({ 0, 0, 1, 0, 0, 0, 0, 0 }));
}

TEST (StemsWords, EachChannelSaysItsCursor)
{
    Stems s;
    s.cursors[2] = 6;
    const auto msgs = said (s);
    EXPECT_EQ (msgs.at ("/channel/3/stem/cursor"), ints ({ 6 }));
    for (const auto& [address, args] : msgs)
    {
        EXPECT_FALSE (address.size() >= 10 && address.compare (address.size() - 10, 10, "/stem/menu") == 0) << address;
        EXPECT_FALSE (address.size() >= 14 && address.compare (address.size() - 14, 14, "/stem/selected") == 0) << address;
    }
}

TEST (StemsWords, TheReturnSaysItsCursorFirstAndItsMode)
{
    Stems s;
    s.returnCursor = 0;
    s.returnMode = 1;
    const auto msgs = said (s);
    EXPECT_EQ (msgs.at ("/aux-return/stem")[0], pyInt (0));
    EXPECT_EQ (msgs.at ("/aux-return/stem/mode"), ints ({ 1 }));
}

// TheTidySettles

TEST (StemsWords, DueOnceAfterTheQuiet)
{
    Settle settle (0.3);
    settle.poke (0.0);
    settle.poke (0.2);
    EXPECT_FALSE (settle.due (0.4));
    EXPECT_TRUE (settle.due (0.51));
    EXPECT_FALSE (settle.due (0.9));
}

TEST (StemsWords, NothingPokedIsNeverDue)
{
    EXPECT_FALSE (Settle (0.3).due (100.0));
}

// WhatStemDeckHears

TEST (StemsWords, PairSixIsDeckTwoStemTwo)
{
    EXPECT_EQ (deckAndStem (6), (std::pair<int, int> { 2, 2 }));
    EXPECT_EQ (pairOf (2, 2), 6);
}

TEST (StemsWords, ACommandIsASwitchAddress)
{
    const auto messages = commandMessages ({ { 6, 5, true } }, b0::truth());
    ASSERT_EQ (messages.size(), 1u);
    EXPECT_EQ (messages[0].first, "/stemdeck/2/2/bus/5");
    EXPECT_EQ (messages[0].second, pyInt (1));
}

// OnlyWhatChanged

TEST (StemsWords, ASecondIdenticalSetSendsNothing)
{
    std::map<std::string, OscArg> memory;
    const std::vector<Message> messages { { "/a", pyFloat (1.0) }, { "/b", pyFloat (0.0) } };
    EXPECT_EQ (changedMessages (messages, memory), messages);
    EXPECT_TRUE (changedMessages (messages, memory).empty());
}
