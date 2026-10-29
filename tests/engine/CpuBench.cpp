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

// Core's DeckSelector puts a deck on one channel at most, so the bench may not either: two decks
// leave two channels on analog, and the deck channels are the first ones.
TEST (CpuBench, EachDeckSitsOnOneChannelAndTheRestAreAnalog)
{
    EXPECT_EQ (benchSourceOfChannel (0, 2), 1);
    EXPECT_EQ (benchSourceOfChannel (1, 2), 2);
    EXPECT_EQ (benchSourceOfChannel (2, 2), 0);
    EXPECT_EQ (benchSourceOfChannel (3, 2), 0);
    for (int c = 0; c < 4; ++c)
        EXPECT_EQ (benchSourceOfChannel (c, 4), c + 1);
    EXPECT_EQ (benchSourceOfChannel (3, 5), 4);   // a fifth deck sits on no channel of four
}

TEST (CpuBench, TheRigsChannelsHoldTheDecksAsTheDeskCan)
{
    for (int decks : { 2, 4 })
    {
        const auto rig = rigWithDecks (decks);
        std::multiset<int> held;
        for (int c = 0; c < 4; ++c)
            held.insert (rig->controls().channel (c).source.load());
        for (int deck = 1; deck <= decks; ++deck)
            EXPECT_EQ (held.count (deck), 1u) << "deck " << deck;
        EXPECT_EQ ((int) held.count (0), 4 - decks);
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

// A slot the bench feeds but the engine ignores would cost nothing and measure nothing.
TEST (CpuBench, DecksCAndDReachTheMainOutput)
{
    const auto fed = rigWithDecks (4);
    const auto cut = rigWithDecks (4);
    float fedEnergy = 0.0f, cutEnergy = 0.0f;
    for (int block = 0; block < 400; ++block)
    {
        auto withoutCD = cut->inputsAt (block);
        for (size_t slot = 2; slot < withoutCD.decks.size(); ++slot)
            withoutCD.decks[slot] = {};
        fed->process (block);
        cut->processWith (withoutCD);
        fedEnergy = fed->mainEnergy();
        cutEnergy = cut->mainEnergy();
    }
    EXPECT_GT (fedEnergy, 0.0f);
    EXPECT_NE (fedEnergy, cutEnergy);
    EXPECT_GT (std::abs (fedEnergy - cutEnergy), 0.01f * fedEnergy);
}
