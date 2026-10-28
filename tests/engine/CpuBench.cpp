// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include <set>
#include "CpuBench.h"
#include "SpatialBusFixture.h"
#include "SpatialTestData.h"

using namespace a3bench;

namespace
{
juce::String shippedTruthText() { return juce::File (A3_TRUTH_COPY).loadFileAsString(); }

std::unique_ptr<CpuBenchRig> rigWithDecks (int decks, int channels = 4)
{
    CpuBenchOptions options;
    options.decks = decks;
    options.channels = channels;
    juce::String error;
    auto rig = CpuBenchRig::create (options, a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory), {}, error);
    EXPECT_TRUE (rig != nullptr) << error.toStdString();
    return rig;
}
}

TEST (CpuBench, TheShareOfTheBlockPeriodIsTheBlockTimeOverFramesPerRate)
{
    EXPECT_NEAR (blockSharePercent (1.0, 128, 48000.0), 37.5, 1e-9);   // period 2.6667 ms
    EXPECT_NEAR (blockSharePercent (1.0, 64, 48000.0), 75.0, 1e-9);
}

TEST (CpuBench, TheDeckCountIsAnArgumentAndZeroMeansTheTruthsCount)
{
    const auto parsed = parseCpuBenchArgs ({ "--decks", "6", "--frames", "64", "--rate", "44100", "--blocks", "100", "--paced" });
    ASSERT_TRUE (parsed.has_value());
    EXPECT_EQ (parsed->decks, 6);
    EXPECT_EQ (parsed->frames, 64);
    EXPECT_EQ (parsed->rate, 44100.0);
    EXPECT_EQ (parsed->blocks, 100);
    EXPECT_TRUE (parsed->paced);

    const auto defaults = parseCpuBenchArgs ({});
    ASSERT_TRUE (defaults.has_value());
    EXPECT_EQ (defaults->decks, 0);
    EXPECT_EQ (defaults->frames, 128);
    EXPECT_EQ (defaults->rate, 48000.0);
}

TEST (CpuBench, ABadArgumentRefusesTheWholeLine)
{
    EXPECT_FALSE (parseCpuBenchArgs ({ "--decks", "0" }).has_value());
    EXPECT_FALSE (parseCpuBenchArgs ({ "--decks", "many" }).has_value());
    EXPECT_FALSE (parseCpuBenchArgs ({ "--decks" }).has_value());
    EXPECT_FALSE (parseCpuBenchArgs ({ "--frames", "0" }).has_value());
    EXPECT_FALSE (parseCpuBenchArgs ({ "--nonsense" }).has_value());
}

TEST (CpuBench, TheTruthsDeckRangeGivesTheDefaultCount)
{
    EXPECT_EQ (deckCountOfTruth (shippedTruthText()), 4);
    EXPECT_EQ (deckCountOfTruth ("{\"addresses\":{}}"), 0);
    EXPECT_EQ (deckCountOfTruth ("not json"), 0);
}

TEST (CpuBench, EveryDeckSlotIsFedOnEveryOneOfItsInputs)
{
    for (int decks : { 2, 4, 5 })
    {
        const auto rig = rigWithDecks (decks);
        ASSERT_TRUE (rig != nullptr);
        const auto in = rig->inputsAt (0);
        ASSERT_EQ ((int) in.decks.size(), decks);
        for (const auto& deck : in.decks)
        {
            for (const auto* p : deck.bformat)
                EXPECT_NE (p, nullptr);
            for (const auto* p : deck.aux)
                EXPECT_NE (p, nullptr);
            for (const auto* p : deck.cue)
                EXPECT_NE (p, nullptr);
        }
    }
}

TEST (CpuBench, EveryDeckSitsOnAChannelWhenThereAreChannelsForThem)
{
    for (int decks : { 2, 4 })
    {
        const auto rig = rigWithDecks (decks);
        std::set<int> held;
        for (int c = 0; c < 4; ++c)
            held.insert (rig->controls().channel (c).source.load());
        EXPECT_EQ ((int) held.size(), decks);
        EXPECT_EQ (held.count (0), 0u) << "a channel on analog would not run a deck";
        EXPECT_GT (rig->controls().channel (0).auxSend.load(), 0.0f);
        EXPECT_GT (rig->controls().channel (0).cue.load(), 0.0f);
    }
}

TEST (CpuBench, ABlockWithEveryInputFedMakesFiniteSoundOnTheMainZone)
{
    const auto rig = rigWithDecks (4);
    for (int block = 0; block < 400; ++block)
        rig->process (block);
    const auto peak = rig->mainPeak();
    EXPECT_TRUE (std::isfinite (peak));
    EXPECT_GT (peak, 1e-4f);
}
