// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "EngineFixture.h"
#include "EnginePorts.h"

namespace
{
using Names = std::vector<std::string>;

Names perChannel (int n)
{
    Names names;
    for (int c = 1; c <= n; ++c)
        for (const char* tail : { "_pre_L", "_pre_R", "_post_L", "_post_R" })
            names.push_back ("ch" + std::to_string (c) + tail);
    return names;
}

Names concat (std::initializer_list<Names> parts)
{
    Names all;
    for (const auto& part : parts)
        all.insert (all.end(), part.begin(), part.end());
    return all;
}
}

TEST (EnginePorts, FourChannelsDefaultData)
{
    const auto config = a3test::engineConfig (4, 0);
    const auto spatial = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    const auto ports = a3app::enginePortNames (config, spatial);

    Names inputs;
    for (int i = 1; i <= 12; ++i)
        inputs.push_back ("analog_" + std::to_string (i));
    EXPECT_EQ (ports.inputs, inputs);

    EXPECT_EQ (ports.outputs, concat ({
        { "main_sub", "main_top_1", "main_top_2", "main_top_3", "main_top_4" },
        { "booth_sub", "booth_top_1", "booth_top_2", "booth_top_3", "booth_top_4" },
        { "rec_L", "rec_R", "phones_L", "phones_R" },
        perChannel (4) }));
}

TEST (EnginePorts, FollowNAndTheLayouts)
{
    auto config = a3test::engineConfig (6, 0);
    config.analogInputs = 8;
    const auto spatial = a3test::loadedSpatialConfig (a3test::reaperBoothSpatialDirectory);
    const auto ports = a3app::enginePortNames (config, spatial);

    EXPECT_EQ (ports.inputs.size(), 8u);
    EXPECT_EQ (ports.inputs.back(), "analog_8");
    EXPECT_EQ (ports.outputs, concat ({
        { "main_sub", "main_top_1", "main_top_2", "main_top_3", "main_top_4" },
        { "booth_sub", "booth_top_1", "booth_top_2", "booth_top_3", "booth_top_4", "booth_top_5", "booth_top_6" },
        { "rec_L", "rec_R", "phones_L", "phones_R" },
        perChannel (6) }));
}

TEST (EnginePorts, TwentyInputsPerDeck)
{
    const auto spatial = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    EXPECT_EQ (a3app::enginePortNames (a3test::engineConfig (4, 0), spatial).inputs.size(), 12u);
    EXPECT_EQ (a3app::enginePortNames (a3test::engineConfig (4, 2), spatial).inputs.size(), 12u + 2 * 20u);

    const auto three = a3app::enginePortNames (a3test::engineConfig (4, 3), spatial).inputs;
    ASSERT_EQ (three.size(), 12u + 3 * 20u);
    // Slot 2 is letter B in port names (R12); the 20 are 16 channels, cue, aux.
    const Names deckB (three.begin() + 12 + 20, three.begin() + 12 + 40);
    Names expected;
    for (int i = 1; i <= 16; ++i)
        expected.push_back ("deckB_" + std::to_string (i));
    for (const char* tail : { "deckB_cue_L", "deckB_cue_R", "deckB_aux_L", "deckB_aux_R" })
        expected.push_back (tail);
    EXPECT_EQ (deckB, expected);
    EXPECT_EQ (three[12], "deckA_1");
    EXPECT_EQ (three.back(), "deckC_aux_R");
}

TEST (EnginePorts, OutputsMatchTheEnginesMeterOrderAndCount)
{
    for (const auto& directory : { a3test::shippedSpatialDirectory, a3test::reaperBoothSpatialDirectory })
        for (const int n : { 1, 4, 6 })
        {
            const auto config = a3test::engineConfig (n, 2);
            const auto spatial = a3test::loadedSpatialConfig (directory);
            juce::String error;
            auto engine = a3::Engine::create (config, spatial, error);
            ASSERT_NE (engine, nullptr) << error;
            engine->prepare (a3test::engineRate, 64);
            const auto ports = a3app::enginePortNames (config, spatial);
            EXPECT_EQ (ports.outputs.size(), engine->outputMeters().size()) << directory.getFileName() << " N=" << n;
            // The order tail is the engine's: pre L/R, post L/R per channel, after rec and phones.
            ASSERT_GE (ports.outputs.size(), 4u + 4u * (size_t) n);
            const Names tail (ports.outputs.end() - 4 * n - 4, ports.outputs.end());
            EXPECT_EQ (tail, concat ({ { "rec_L", "rec_R", "phones_L", "phones_R" }, perChannel (n) }));
        }
}
