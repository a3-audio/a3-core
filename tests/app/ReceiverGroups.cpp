// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <gtest/gtest.h>
#include "EngineFixture.h"
#include "EnginePorts.h"
#include "ReceiverGroups.h"

namespace
{
using Groups = std::vector<a3app::ReceiverGroup>;

const a3app::ReceiverGroup* find (const Groups& groups, const std::string& name)
{
    const auto it = std::find_if (groups.begin(), groups.end(), [&] (const auto& g) { return g.name == name; });
    return it == groups.end() ? nullptr : &*it;
}
}

TEST (ReceiverGroups, EveryPortIsAnEngineInput)
{
    const auto spatial = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    for (int decks : { 1, 4, 27 })
    {
        const auto config = a3test::engineConfig (4, decks);
        const auto inputs = a3app::enginePortNames (config, spatial).inputs;
        const auto groups = a3app::receiverGroups (config);
        EXPECT_FALSE (groups.empty());
        for (const auto& group : groups)
            for (const auto& port : group.ports)
                EXPECT_NE (std::find (inputs.begin(), inputs.end(), port), inputs.end()) << group.name << " " << port;
    }
}

TEST (ReceiverGroups, BFormatAndReturnAreSixteenPorts)
{
    const auto groups = a3app::receiverGroups (a3test::engineConfig (4, 2));
    const auto* bformat = find (groups, "deck B B-format");
    const auto* ret = find (groups, "deck B return B-format");
    ASSERT_NE (bformat, nullptr);
    ASSERT_NE (ret, nullptr);
    ASSERT_EQ (bformat->ports.size(), 16u);
    ASSERT_EQ (ret->ports.size(), 16u);
    EXPECT_EQ (bformat->ports.front(), "deckB_1");
    EXPECT_EQ (bformat->ports.back(), "deckB_16");
    EXPECT_EQ (ret->ports.front(), "deckB_aux_1");
    EXPECT_EQ (ret->ports.back(), "deckB_aux_16");
    EXPECT_FALSE (bformat->sharedWithSoundCard);
    EXPECT_FALSE (ret->sharedWithSoundCard);
}

TEST (ReceiverGroups, StereoGroupsAreAnalogPairs)
{
    const auto groups = a3app::receiverGroups (a3test::engineConfig (4, 0));
    const auto* pair = find (groups, "analog 3-4");
    ASSERT_NE (pair, nullptr);
    EXPECT_EQ (pair->ports, (std::vector<std::string> { "analog_3", "analog_4" }));
    const auto* ret = find (groups, "return stereo");
    ASSERT_NE (ret, nullptr);
    EXPECT_EQ (ret->ports, (std::vector<std::string> { "analog_11", "analog_12" }));
    EXPECT_EQ (groups.size(), 7u);   // six pairs + return stereo
}

TEST (ReceiverGroups, OnlyCompleteAnalogPairsAndNoReturnBelowTwelveInputs)
{
    auto config = a3test::engineConfig (4, 0);
    config.analogInputs = 5;
    const auto groups = a3app::receiverGroups (config);
    EXPECT_NE (find (groups, "analog 3-4"), nullptr);
    EXPECT_EQ (find (groups, "analog 5-6"), nullptr);
    EXPECT_EQ (find (groups, "return stereo"), nullptr);
    EXPECT_EQ (groups.size(), 2u);
}

TEST (ReceiverGroups, AnalogGroupsSayTheyAreSharedWithTheSoundCard)
{
    for (const auto& group : a3app::receiverGroups (a3test::engineConfig (4, 2)))
    {
        const bool analog = group.ports.front().rfind ("analog_", 0) == 0;
        EXPECT_EQ (group.sharedWithSoundCard, analog) << group.name;
    }
}

TEST (ReceiverGroups, DeckLettersFollowTheEngine)
{
    const auto groups = a3app::receiverGroups (a3test::engineConfig (4, 27));
    const auto* last = find (groups, "deck AA B-format");
    ASSERT_NE (last, nullptr);
    EXPECT_EQ (last->ports.front(), "deckAA_1");
    EXPECT_NE (find (groups, "deck Z return B-format"), nullptr);
}
