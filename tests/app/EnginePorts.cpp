// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
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

TEST (EnginePorts, DeckSlotHas41Inputs)
{
    const auto spatial = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    EXPECT_EQ (a3app::enginePortNames (a3test::engineConfig (4, 0), spatial).inputs.size(), 12u);
    const auto two = a3app::enginePortNames (a3test::engineConfig (4, 2), spatial).inputs;
    ASSERT_EQ (two.size(), 12u + 2 * 41u);

    // Slot 2 is letter B in port names (R12): 16 channels, 16 aux, 9 cue.
    const Names deckB (two.begin() + 12 + 41, two.end());
    Names expected;
    for (int i = 1; i <= 16; ++i)
        expected.push_back ("deckB_" + std::to_string (i));
    for (int i = 1; i <= 16; ++i)
        expected.push_back ("deckB_aux_" + std::to_string (i));
    for (int i = 1; i <= 9; ++i)
        expected.push_back ("deckB_cue_" + std::to_string (i));
    EXPECT_EQ (deckB, expected);
    EXPECT_EQ (two[12], "deckA_1");
    EXPECT_EQ (two.back(), "deckB_cue_9");
    EXPECT_EQ (std::count (two.begin(), two.end(), "deckA_cue_L"), 0);
}

// A8: four slots (A-D) and six (A-F) are the same walk; 4 slots are 12 + 4 x 41 = 176 inputs.
TEST (EnginePorts, FourAndSixSlotsFollowTheCount)
{
    const auto spatial = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    const auto four = a3app::enginePortNames (a3test::engineConfig (4, 4), spatial).inputs;
    ASSERT_EQ (four.size(), 176u);
    EXPECT_EQ (four[12 + 2 * 41], "deckC_1");
    EXPECT_EQ (four[12 + 3 * 41 + 16], "deckD_aux_1");
    EXPECT_EQ (four.back(), "deckD_cue_9");
    const auto six = a3app::enginePortNames (a3test::engineConfig (4, 6), spatial).inputs;
    ASSERT_EQ (six.size(), 12u + 6 * 41u);
    EXPECT_EQ (six[12 + 4 * 41], "deckE_1");
    EXPECT_EQ (six.back(), "deckF_cue_9");
}

// One pure function names a slot, with no cap (A..Z, AA, AB, ...), and reads it back in either case.
TEST (EnginePorts, DeckLettersHaveNoCapAndReadBack)
{
    EXPECT_EQ (a3::deckLetters (1), "A");
    EXPECT_EQ (a3::deckLetters (4), "D");
    EXPECT_EQ (a3::deckLetters (26), "Z");
    EXPECT_EQ (a3::deckLetters (27), "AA");
    EXPECT_EQ (a3::deckLetters (28), "AB");
    EXPECT_EQ (a3::deckLetters (53), "BA");
    EXPECT_EQ (a3::deckLetters (703), "AAA");
    for (int slot = 1; slot <= 1000; ++slot)
        ASSERT_EQ (a3::deckSlotOf (a3::deckLetters (slot)), slot) << slot;
    EXPECT_EQ (a3::deckSlotOf ("c"), 3);
    EXPECT_EQ (a3::deckSlotOf ("aa"), 27);
    for (const char* other : { "", "1", "a1", "_", "A-" })
        EXPECT_EQ (a3::deckSlotOf (other), 0) << other;
    EXPECT_EQ (a3::deckSlotOfMeter ("deck_d"), 4);
    EXPECT_EQ (a3::deckSlotOfMeter ("deck_aa"), 27);
    for (const char* other : { "deck_", "deckA", "deck_a1", "free39", "aux_L" })
        EXPECT_EQ (a3::deckSlotOfMeter (other), 0) << other;
}

TEST (EnginePorts, RetiredDeckPortsAreTheOldStereoPairs)
{
    const Names expected { "deckA_cue_L", "deckA_cue_R", "deckA_aux_L", "deckA_aux_R",
                           "deckB_cue_L", "deckB_cue_R", "deckB_aux_L", "deckB_aux_R" };
    EXPECT_EQ (a3app::retiredDeckPorts (2), expected);
    EXPECT_TRUE (a3app::retiredDeckPorts (0).empty());
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
            // The head is the zones' port order as Engine::buildMeters walks it: sub first, then tops.
            const auto zoneNames = [] (const char* zone, const a3::Layout& layout)
            {
                Names names;
                if (layout.hasSub())
                    names.push_back (std::string (zone) + "_sub");
                for (size_t k = 1; k <= layout.topChannels.size(); ++k)
                    names.push_back (std::string (zone) + "_top_" + std::to_string (k));
                return names;
            };
            const auto head = concat ({ zoneNames ("main", spatial.main.layout), zoneNames ("booth", spatial.booth.layout) });
            ASSERT_EQ (ports.outputs.size(), head.size() + 4u + 4u * (size_t) n);
            EXPECT_EQ (Names (ports.outputs.begin(), ports.outputs.begin() + (long) head.size()), head);
            // The order tail is the engine's: pre L/R, post L/R per channel, after rec and phones.
            ASSERT_GE (ports.outputs.size(), 4u + 4u * (size_t) n);
            const Names tail (ports.outputs.end() - 4 * n - 4, ports.outputs.end());
            EXPECT_EQ (tail, concat ({ { "rec_L", "rec_R", "phones_L", "phones_R" }, perChannel (n) }));
        }
}

TEST (EnginePorts, InputsAreTheAnalogAndDeckPorts)
{
    const auto spatial = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    const auto ports = a3app::enginePortNames (a3test::engineConfig (6, 3), spatial);
    for (const auto& name : ports.inputs)
        EXPECT_TRUE (a3app::isEngineInputPort (name)) << name;
    for (const auto& name : ports.outputs)
        EXPECT_FALSE (a3app::isEngineInputPort (name)) << name;
}

// Each port's buffer lands where the engine reads or writes that port: the slots walk
// exactly enginePortNames' order, so a port named X is the engine's X.
TEST (EnginePorts, SlotsFollowTheNames)
{
    for (const auto& directory : { a3test::shippedSpatialDirectory, a3test::reaperBoothSpatialDirectory })
    {
        const auto config = a3test::engineConfig (5, 2);
        const auto spatial = a3test::loadedSpatialConfig (directory);
        const auto names = a3app::enginePortNames (config, spatial);
        a3::EngineInputs in;
        a3::EngineOutputs out;
        const auto slots = a3app::bindPortSlots (config, spatial, in, out);
        ASSERT_EQ (slots.inputs.size(), names.inputs.size());
        ASSERT_EQ (slots.outputs.size(), names.outputs.size());

        // A distinct fake buffer per port, recognisable by its index.
        std::vector<float> marks (names.inputs.size() + names.outputs.size());
        for (size_t i = 0; i < slots.inputs.size(); ++i)
            *slots.inputs[i] = &marks[i];
        for (size_t i = 0; i < slots.outputs.size(); ++i)
            *slots.outputs[i] = &marks[names.inputs.size() + i];
        const auto inputNamed = [&] (const std::string& name)
        {
            const auto at = std::find (names.inputs.begin(), names.inputs.end(), name);
            return &marks[(size_t) (at - names.inputs.begin())];
        };
        const auto outputNamed = [&] (const std::string& name)
        {
            const auto at = std::find (names.outputs.begin(), names.outputs.end(), name);
            return &marks[names.inputs.size() + (size_t) (at - names.outputs.begin())];
        };

        ASSERT_EQ (in.analog.size(), (size_t) config.analogInputs);
        ASSERT_EQ (in.decks.size(), 2u);
        EXPECT_EQ (in.analog[0], inputNamed ("analog_1"));
        EXPECT_EQ (in.analog[11], inputNamed ("analog_12"));
        EXPECT_EQ (in.decks[0].bformat[0], inputNamed ("deckA_1"));
        EXPECT_EQ (in.decks[1].bformat[15], inputNamed ("deckB_16"));
        EXPECT_EQ (in.decks[1].cue[8], inputNamed ("deckB_cue_9"));
        EXPECT_EQ (in.decks[1].aux[15], inputNamed ("deckB_aux_16"));
        EXPECT_EQ (in.decks[0].aux[0], inputNamed ("deckA_aux_1"));
        EXPECT_EQ (in.decks[0].cue[0], inputNamed ("deckA_cue_1"));

        ASSERT_EQ (out.main.tops.size(), spatial.main.layout.topChannels.size());
        ASSERT_EQ (out.booth.tops.size(), spatial.booth.layout.topChannels.size());
        ASSERT_EQ (out.pre.size(), 5u);
        ASSERT_EQ (out.post.size(), 5u);
        EXPECT_EQ (out.main.sub, spatial.main.layout.hasSub() ? outputNamed ("main_sub") : nullptr);
        EXPECT_EQ (out.main.tops.back(), outputNamed ("main_top_" + std::to_string (out.main.tops.size())));
        EXPECT_EQ (out.booth.tops[0], outputNamed ("booth_top_1"));
        EXPECT_EQ (out.rec[1], outputNamed ("rec_R"));
        EXPECT_EQ (out.phones[0], outputNamed ("phones_L"));
        EXPECT_EQ (out.pre[0][0], outputNamed ("ch1_pre_L"));
        EXPECT_EQ (out.post[4][1], outputNamed ("ch5_post_R"));
    }
}

TEST (EnginePorts, SlotsBindAuxAndCue)
{
    const auto config = a3test::engineConfig (4, 2);
    const auto spatial = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    a3::EngineInputs in;
    a3::EngineOutputs out;
    const auto slots = a3app::bindPortSlots (config, spatial, in, out);
    std::vector<float> marks (slots.inputs.size());
    for (size_t i = 0; i < slots.inputs.size(); ++i)
        *slots.inputs[i] = &marks[i];
    EXPECT_EQ (in.decks[1].aux[15], &marks[12 + 41 + 16 + 15]);
    EXPECT_EQ (in.decks[1].cue[8], &marks[12 + 41 + 32 + 8]);
}
