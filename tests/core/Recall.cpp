// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_recall.py (B0 f3345dc7feeb): Core says its state once
// more, as the messages that set it. The source-text case (every filter mode has a word, read out
// of a3-core.py) belongs to the task that wires the program. The whole answer for the rig's real
// files is recorded by tools/parity/record_state.py, values and types.
#include <gtest/gtest.h>

#include <set>

#include "B0Truth.h"
#include "Recall.h"
#include "StateRecorded.h"

using namespace a3core;
using Messages = std::vector<std::pair<std::string, OscArg>>;
using recorded::aRig;

namespace
{
// A rig's lamps: two a channel, the filter's and the return's cue. Its flags: two a channel and the mode.
constexpr size_t lamps = 4 * 2 + 2;
constexpr size_t flags = 4 * 2 + 1;

Messages lampsOf (const std::vector<ChannelInfo>& channels, const MasterInfo& master) { return lampMessages (b0::truth(), channels, master); }
Messages flagsOf (const std::vector<ChannelInfo>& channels, const MasterInfo& master) { return flagMessages (b0::truth(), channels, master); }

OscArg find (const Messages& messages, const std::string& address)
{
    for (const auto& [at, value] : messages)
        if (at == address)
            return value;
    ADD_FAILURE() << address << " is not in the messages";
    return {};
}

bool contains (const Messages& messages, const std::string& address, const OscArg& value)
{
    return std::find (messages.begin(), messages.end(), std::pair<std::string, OscArg> { address, value }) != messages.end();
}

std::vector<ChannelInfo> channelsWith (std::initializer_list<ChannelInfo> list) { return { list }; }

ChannelInfo at (std::optional<double> azimuth, std::optional<double> elevation = {}, std::optional<double> threeD = {})
{
    ChannelInfo channel;
    channel.azimuth = azimuth;
    channel.elevation = elevation;
    channel.threeD = threeD;
    return channel;
}
}

// TheLights

TEST (Recall, ALightIsAddressedTheWayTheTruthSays)
{
    ChannelInfo channel;
    channel.toggleFx = true;
    EXPECT_EQ (ledMessage (b0::truth(), LedFlag::fx, 2, channel), (std::pair<std::string, OscArg> { "/channel/3/filter/led", pyFloat (1.0) }));
}

TEST (Recall, ALampSaysWhetherItIsLit)
{
    ChannelInfo lit, dark;
    lit.toggleCue = true;
    EXPECT_EQ (ledMessage (b0::truth(), LedFlag::cue, 0, lit).second, pyFloat (1.0));
    EXPECT_EQ (ledMessage (b0::truth(), LedFlag::cue, 0, dark).second, pyFloat (0.0));
}

TEST (Recall, NoLampIsInvertedAnyMore)
{
    ChannelInfo on;
    on.toggleCue = true;
    on.toggleFx = true;
    for (const auto flag : ledFlags)
        EXPECT_EQ (ledMessage (b0::truth(), flag, 0, on).second, pyFloat (1.0));
}

TEST (Recall, TheLampsOfARigAreTwoAChannelTheFilterAndTheReturnCue)
{
    auto rig = aRig();
    EXPECT_EQ (lampsOf (rig.channels, rig.master).size(), lamps);
}

TEST (Recall, TheReturnCueLampSaysWhetherItIsLit)
{
    auto rig = aRig();
    for (const bool on : { false, true })
    {
        rig.master.returnCue = on;
        EXPECT_EQ (find (lampsOf (rig.channels, rig.master), "/aux-return/cue/led"), pyFloat (on ? 1.0 : 0.0));
        EXPECT_EQ (returnCueLamp (b0::truth(), rig.master), (std::pair<std::string, OscArg> { "/aux-return/cue/led", pyFloat (on ? 1.0 : 0.0) }));
    }
}

TEST (Recall, TheFlagsOfARigAreTwoAChannelAndTheMode)
{
    auto rig = aRig();
    EXPECT_EQ (flagsOf (rig.channels, rig.master).size(), flags);
}

TEST (Recall, TheLampSaysTheModeAsAWordAndTheFlagAsANumber)
{
    auto rig = aRig();
    rig.master.fxMode = MasterInfo::FxMode::highPass;
    EXPECT_TRUE (contains (lampsOf (rig.channels, rig.master), "/filter/led", pyStr ("high_pass")));
    EXPECT_TRUE (contains (flagsOf (rig.channels, rig.master), "/filter/mode", pyFloat (1.0)));
}

TEST (Recall, TheLampAndTheFlagNowAgree)
{
    auto rig = aRig();
    rig.channels[0].toggleCue = true;
    EXPECT_EQ (find (flagsOf (rig.channels, rig.master), "/channel/1/cue"), pyFloat (1.0));
    EXPECT_EQ (find (lampsOf (rig.channels, rig.master), "/channel/1/cue/led"), pyFloat (1.0));
}

TEST (Recall, TheFlagCarriesNoLampAddress)
{
    auto rig = aRig();
    for (const auto& [address, value] : flagsOf (rig.channels, rig.master))
        EXPECT_FALSE (address.size() >= 4 && address.compare (address.size() - 4, 4, "/led") == 0) << address;
}

TEST (Recall, 4dIsGoneAnd3dWasNeverAFlag)
{
    auto rig = aRig();
    for (const auto& [address, value] : flagsOf (rig.channels, rig.master))
    {
        EXPECT_NE (address, "/channel/1/4d");
        EXPECT_NE (address, "/channel/1/3d");
    }
}

TEST (Recall, TwoFlagsAChannelAndBothAreKeysSomebodyCanPress)
{
    auto rig = aRig();
    std::set<std::string> suffixes;
    for (const auto& [address, value] : flagsOf (rig.channels, rig.master))
        suffixes.insert (address.substr (address.rfind ('/') + 1));
    EXPECT_EQ (suffixes, (std::set<std::string> { "cue", "filter", "mode" }));
}

// WhatTheSourceSays

TEST (Recall, EveryLightTheTruthHasIsReplayed)
{
    // Two are per channel and belong to a flag; the filter mode's and the return cue's are the master's.
    std::set<std::string> named, replayed { "filter.led", "aux-return.cue.led" };
    const auto addresses = b0::truth().data()["addresses"];
    for (const auto& property : addresses.getDynamicObject()->getProperties())
    {
        const auto key = property.name.toString().toStdString();
        if (key.size() >= 4 && key.compare (key.size() - 4, 4, ".led") == 0)
            named.insert (key);
    }
    for (const auto flag : ledFlags)
        replayed.insert (ledKey (flag));
    EXPECT_EQ (named, replayed);
}

// WhatWasPassedOn

TEST (Recall, NothingRelayedIsNothingToReplay)
{
    EXPECT_TRUE (Relayed().messages().empty());
}

TEST (Recall, TheLastValueWins)
{
    Relayed relayed;
    relayed.note ("/channel/0/gain", pyFloat (0.2));
    relayed.note ("/channel/0/gain", pyFloat (0.7));
    EXPECT_EQ (relayed.messages(), (std::vector<OscMessage> { { "/channel/0/gain", { pyFloat (0.7) } } }));
}

TEST (Recall, ALaterValueDoesNotMoveItsPlace)
{
    Relayed relayed;
    relayed.note ("/channel/0/gain", pyFloat (0.2));
    relayed.note ("/channel/1/3d", pyFloat (0.5));
    relayed.note ("/channel/0/gain", pyFloat (0.7));
    const auto messages = relayed.messages();
    ASSERT_EQ (messages.size(), 2u);
    EXPECT_EQ (messages[0].address, "/channel/0/gain");
    EXPECT_EQ (messages[1].address, "/channel/1/3d");
}

TEST (Recall, OneAddressIsOneValueHoweverManyHearIt)
{
    Relayed relayed;
    relayed.note ("/channel/0/gain", pyFloat (0.2));
    relayed.note ("/channel/0/gain", pyFloat (0.9));
    EXPECT_EQ (relayed.messages().size(), 1u);
    EXPECT_EQ (relayed.size(), 1u);
}

TEST (Recall, ItSaysTheLastValueOfAnAddressAndNoneBeforeAny)
{
    // B1 (a3-core 7a7b2441f74e): Relayed.value, what a channel back on analog re-sends to the engine.
    Relayed relayed;
    EXPECT_EQ (relayed.value ("/channel/3/filter/q"), std::nullopt);
    relayed.note ("/channel/3/filter/q", pyFloat (0.75));
    relayed.note ("/channel/3/filter/q", pyFloat (0.5));
    EXPECT_EQ (relayed.value ("/channel/3/filter/q"), std::optional<OscArg> (pyFloat (0.5)));
    EXPECT_EQ (relayed.value ("/channel/3/filter/frequency"), std::nullopt);
}

TEST (Recall, ItCanSayWhetherItAlreadyHoldsAValue)
{
    Relayed relayed;
    EXPECT_FALSE (relayed.holds ("/channel/0/gain", pyFloat (0.5)));
    relayed.note ("/channel/0/gain", pyFloat (0.5));
    EXPECT_TRUE (relayed.holds ("/channel/0/gain", pyFloat (0.5)));
    EXPECT_FALSE (relayed.holds ("/channel/0/gain", pyFloat (0.6)));
}

TEST (Recall, AValueIsHeldTheWayPythonComparesIt)
{
    // 0.5 == 0.5 whatever holds it; 1 == 1.0 == True; a text is never a number.
    Relayed relayed;
    relayed.note ("/a", pyFloat (1.0));
    relayed.note ("/b", pyStr ("1"));
    EXPECT_TRUE (relayed.holds ("/a", pyInt (1)));
    EXPECT_TRUE (relayed.holds ("/a", pyBool (true)));
    EXPECT_TRUE (relayed.holds ("/a", OscArg { 'f', 1.0f }));
    EXPECT_FALSE (relayed.holds ("/b", pyInt (1)));
    EXPECT_TRUE (relayed.holds ("/b", pyStr ("1")));
    EXPECT_FALSE (relayed.holds ("/c", pyStr ("1")));
}

// WhereTheSoundIs

TEST (Recall, APositionReplaysAsTheMessageMotionSent)
{
    EXPECT_EQ (rememberedMessages (b0::truth(), channelsWith ({ at (-37.5, 12.0) })),
               (Messages { { "/channel/1/azimuth", pyFloat (-37.5) }, { "/channel/1/elevation", pyFloat (12.0) } }));
}

TEST (Recall, APositionNeverSeenIsNotInvented)
{
    EXPECT_TRUE (rememberedMessages (b0::truth(), channelsWith ({ ChannelInfo() })).empty());
}

TEST (Recall, OneHalfKnownIsAnsweredByThatHalf)
{
    EXPECT_EQ (rememberedMessages (b0::truth(), channelsWith ({ at (90.0) })), (Messages { { "/channel/1/azimuth", pyFloat (90.0) } }));
}

TEST (Recall, ZeroIsAPositionAndIsAnswered)
{
    EXPECT_EQ (rememberedMessages (b0::truth(), channelsWith ({ at (0.0, 0.0) })),
               (Messages { { "/channel/1/azimuth", pyFloat (0.0) }, { "/channel/1/elevation", pyFloat (0.0) } }));
}

TEST (Recall, EachChannelIsAddressedAsItself)
{
    EXPECT_EQ (rememberedMessages (b0::truth(), channelsWith ({ ChannelInfo(), at (5.0), ChannelInfo(), at ({}, -90.0) })),
               (Messages { { "/channel/2/azimuth", pyFloat (5.0) }, { "/channel/4/elevation", pyFloat (-90.0) } }));
}

TEST (Recall, TheCrossfadeIsRememberedTooAndForItsOwnReason)
{
    EXPECT_EQ (rememberedMessages (b0::truth(), channelsWith ({ at ({}, {}, 0.62) })), (Messages { { "/channel/1/3d", pyFloat (0.62) } }));
}

TEST (Recall, ACrossfadeNeverSeenIsNotInventedEither)
{
    EXPECT_TRUE (rememberedMessages (b0::truth(), channelsWith ({ ChannelInfo() })).empty());
}

TEST (Recall, AllThreeComeInTheOrderTheyAreSentIn)
{
    const auto messages = rememberedMessages (b0::truth(), channelsWith ({ at (10.0, 20.0, 0.3) }));
    ASSERT_EQ (messages.size(), 3u);
    EXPECT_EQ (messages[0].first, "/channel/1/azimuth");
    EXPECT_EQ (messages[1].first, "/channel/1/elevation");
    EXPECT_EQ (messages[2].first, "/channel/1/3d");
}

TEST (Recall, ThePositionGoesOutLikeEverythingElse)
{
    EXPECT_EQ (rememberedMessages (b0::truth(), channelsWith ({ at (1.0) })), (Messages { { "/channel/1/azimuth", pyFloat (1.0) } }));
}

TEST (Recall, AListIsHeldAsAList)
{
    // B1 (a3-core 7a7b2441f74e): broadcast() notes the return's word, nine integers, as one list value;
    // a recall says it again as nine arguments, as python-osc sends a list.
    Relayed relayed;
    const std::vector<OscArg> nine { pyInt (1), pyInt (0), pyInt (0), pyInt (0), pyInt (0), pyInt (0), pyInt (0), pyInt (0), pyInt (0) };
    relayed.note ("/aux-return/stem", nine);
    EXPECT_TRUE (relayed.holds ("/aux-return/stem", nine));
    auto asFloats = nine;
    asFloats[0] = pyFloat (1.0);   // Python: [1, 0] == [1.0, 0]
    EXPECT_TRUE (relayed.holds ("/aux-return/stem", asFloats));
    auto other = nine;
    other[8] = pyInt (1);
    EXPECT_FALSE (relayed.holds ("/aux-return/stem", other));
    EXPECT_FALSE (relayed.holds ("/aux-return/stem", pyInt (1)));   // a list is never one number
    EXPECT_TRUE (relayed.isList ("/aux-return/stem"));
    EXPECT_EQ (relayed.value ("/aux-return/stem"), std::nullopt);
    EXPECT_EQ (relayed.messages(), (std::vector<OscMessage> { { "/aux-return/stem", nine } }));

    relayed.note ("/channel/1/gain", pyFloat (0.5));
    EXPECT_FALSE (relayed.holds ("/channel/1/gain", std::vector<OscArg> { pyFloat (0.5) }));   // [0.5] != 0.5
    EXPECT_FALSE (relayed.isList ("/channel/1/gain"));
}

// TheWholeAnswer

TEST (Recall, ARecallIsTheFlagsAndThenWhatReaperSaid)
{
    auto rig = aRig();
    Relayed relayed;
    relayed.note ("/channel/0/gain", pyFloat (0.7));
    const auto messages = recallMessages (b0::truth(), rig.channels, rig.master, relayed);
    EXPECT_EQ (messages.back(), (OscMessage { "/channel/0/gain", { pyFloat (0.7) } }));
    EXPECT_EQ (messages.size(), lamps + flags + 1);
}

TEST (Recall, ThePositionIsAnsweredBetweenTheFlagsAndReaper)
{
    auto rig = aRig();
    rig.channels[1].azimuth = 45.0;
    Relayed relayed;
    relayed.note ("/channel/0/gain", pyFloat (0.7));
    const auto messages = recallMessages (b0::truth(), rig.channels, rig.master, relayed);
    EXPECT_EQ (messages[lamps + flags], (OscMessage { "/channel/2/azimuth", { pyFloat (45.0) } }));
    EXPECT_EQ (messages.back(), (OscMessage { "/channel/0/gain", { pyFloat (0.7) } }));
    EXPECT_EQ (messages.size(), lamps + flags + 1 + 1);
}

TEST (Recall, AColdCoreStillAnswersWithItsOwnFlags)
{
    auto rig = aRig();
    EXPECT_EQ (recallMessages (b0::truth(), rig.channels, rig.master, Relayed()).size(), lamps + flags);
}

TEST (Recall, AColdCoreAnswersNoPositionEither)
{
    auto rig = aRig();
    for (const auto& message : recallMessages (b0::truth(), rig.channels, rig.master, Relayed()))
    {
        EXPECT_EQ (message.address.find ("azimuth"), std::string::npos);
        EXPECT_EQ (message.address.find ("elevation"), std::string::npos);
    }
}

// The rig's real files

TEST (Recall, MessagesAsPython)
{
    const auto cases = recorded::load ("recall.json");
    const auto states = recorded::load ("state.json");
    ASSERT_GE (cases.getArray()->size(), 16);
    for (const auto& entry : *cases.getArray())
    {
        auto rig = recorded::rigFrom (recorded::text (states[juce::Identifier (entry["state"].toString())]["text"]));
        const auto& where = *entry["positions"].getArray();
        for (int i = 0; i < where.size(); ++i)
        {
            const auto& pair = *where.getReference (i).getArray();
            if (! pair[0].isVoid()) rig.channels[static_cast<size_t> (i)].azimuth = static_cast<double> (pair[0]);
            if (! pair[1].isVoid()) rig.channels[static_cast<size_t> (i)].elevation = static_cast<double> (pair[1]);
        }
        Relayed relayed;
        for (const auto& note : *entry["relayed"].getArray())
            relayed.note ((*note.getArray())[0].toString().toStdString(),
                          recorded::expectedArg ((*note.getArray())[1].toString(), (*note.getArray())[2]));

        const auto got = recallMessages (b0::truth(), rig.channels, rig.master, relayed);
        const auto label = entry["state"].toString() + "/" + entry["relay_name"].toString() + "/" + entry["position_name"].toString();
        const auto& expected = *entry["messages"].getArray();
        ASSERT_EQ (got.size(), static_cast<size_t> (expected.size())) << label;
        for (size_t i = 0; i < got.size(); ++i)
        {
            const auto& item = *expected.getReference (static_cast<int> (i)).getArray();
            EXPECT_EQ (got[i].address, item[0].toString().toStdString()) << label << " " << i;
            ASSERT_EQ (got[i].args.size(), 1u) << label << " " << got[i].address;
            EXPECT_EQ (got[i].args[0], recorded::expectedArg (item[1].toString(), item[2])) << label << " " << got[i].address;
        }
    }
}
