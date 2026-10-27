// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// `a3-core --links`: the graph's connections as a beginner reads them. A pure function, so the graphs here
// are lists of names; the JACK reading is a thin layer that has no test of its own.
#include <gtest/gtest.h>
#include <string>
#include "LinksOverview.h"

namespace
{
using a3app::Link;
using Lines = std::vector<std::string>;

struct Graph
{
    std::vector<Link> links;
    std::vector<std::string> ports;

    void link (const std::string& from, const std::string& to)
    {
        links.push_back ({ from, to });
        for (const auto& name : { from, to })
            if (std::find (ports.begin(), ports.end(), name) == ports.end())
                ports.push_back (name);
    }
};

a3app::LinkMeanings meanings() { return { a3::EngineConfig {}, { "main_sub", "main_top1", "phones_L", "rec_L" } }; }

// On the rig the Midi-Bridge lists the card, which is how "system" is known for the Scarlett.
std::string show (Graph graph, std::optional<std::size_t> missing = 0, bool cardListed = true)
{
    if (cardListed)
        graph.ports.push_back ("Midi-Bridge:Scarlett 18i20 USB: MIDI 1 (capture)");
    return a3app::formatLinks ({ graph.links, graph.ports, missing }, meanings());
}

bool hasLine (const std::string& text, const std::string& line)
{
    return ("\n" + text).find ("\n" + line + "\n") != std::string::npos;
}

// Scarlett capture 1-8 and 11-12 into the engine, as the rig has them.
Graph inputs()
{
    Graph graph;
    for (int n = 1; n <= 8; ++n)
        graph.link ("system:capture_" + std::to_string (n), "a3-engine:analog_" + std::to_string (n));
    graph.link ("system:capture_11", "a3-engine:analog_11");
    graph.link ("system:capture_12", "a3-engine:analog_12");
    return graph;
}
}

TEST (LinksOverview, ParallelPortsCollapseIntoARangeWithTheirMeaning)
{
    const auto text = show (inputs());
    EXPECT_TRUE (hasLine (text, "Scarlett inputs → engine")) << text;
    EXPECT_TRUE (hasLine (text, "  capture 1–8   → analog 1–8   (channels 1–4, L/R)")) << text;
    EXPECT_TRUE (hasLine (text, "  capture 11–12 → analog 11–12 (return)")) << text;
}

TEST (LinksOverview, OutputsNameThePaThePhonesAndTheTempo)
{
    Graph graph;
    graph.link ("a3-engine:main_sub", "system:playback_1");
    for (int k = 1; k <= 4; ++k)
        graph.link ("a3-engine:main_top_" + std::to_string (k), "system:playback_" + std::to_string (k + 1));
    graph.link ("a3-engine:booth_sub", "system:playback_11");
    graph.link ("a3-engine:booth_top_1", "system:playback_12");
    graph.link ("a3-engine:phones_L", "system:playback_9");
    graph.link ("a3-engine:phones_R", "system:playback_10");
    graph.link ("a3-engine:rec_L", "beat-analyzer:bpm_1");
    const auto text = show (graph);
    EXPECT_TRUE (hasLine (text, "engine → Scarlett")) << text;
    EXPECT_TRUE (hasLine (text, "  main 1–5   → playback 1–5   (PA)")) << text;
    EXPECT_TRUE (hasLine (text, "  booth 1–2  → playback 11–12 (booth)")) << text;
    EXPECT_TRUE (hasLine (text, "  phones L/R → playback 9–10  (phones)")) << text;
    EXPECT_TRUE (hasLine (text, "engine → beat-analyzer")) << text;
    EXPECT_TRUE (hasLine (text, "  rec L → bpm 1 (tempo)")) << text;
}

TEST (LinksOverview, AGapOrACrossingOnEitherSideBreaksTheRange)
{
    Graph graph;
    graph.link ("system:capture_1", "a3-engine:analog_1");
    graph.link ("system:capture_3", "a3-engine:analog_2");   // the source skips one
    graph.link ("system:capture_4", "a3-engine:analog_4");   // the target skips one
    const auto text = show (graph);
    EXPECT_TRUE (hasLine (text, "  capture 1 → analog 1 (channel 1, L)")) << text;
    EXPECT_TRUE (hasLine (text, "  capture 3 → analog 2 (channel 1, R)")) << text;
    EXPECT_TRUE (hasLine (text, "  capture 4 → analog 4 (channel 2, R)")) << text;
}

TEST (LinksOverview, StemDeckSlotsAreNamedAndItsDeckPortsExplained)
{
    Graph graph;
    // A slot's 41 inputs, in the engine's order: B-format 16, aux 16, cue 9.
    for (int n = 1; n <= 16; ++n)
        graph.link ("StemDeck-A:bformat_" + std::to_string (n), "a3-engine:deckA_" + std::to_string (n));
    for (int n = 1; n <= 16; ++n)
        graph.link ("StemDeck-A:aux_" + std::to_string (n), "a3-engine:deckA_aux_" + std::to_string (n));
    for (int n = 1; n <= 9; ++n)
        graph.link ("StemDeck-A:cue_" + std::to_string (n), "a3-engine:deckA_cue_" + std::to_string (n));
    const auto text = show (graph);
    EXPECT_TRUE (hasLine (text, "StemDeck A → engine")) << text;
    EXPECT_TRUE (hasLine (text, "  bformat 1–16 → deck A 1–16     (B-format)")) << text;
    EXPECT_TRUE (hasLine (text, "  aux 1–16     → deck A aux 1–16 (aux)")) << text;
    EXPECT_TRUE (hasLine (text, "  cue 1–9      → deck A cue 1–9  (cue)")) << text;
}

TEST (LinksOverview, TheCardsLongAlsaNameAndSystemAreBothTheScarlett)
{
    Graph alsa;
    alsa.link ("Scarlett 18i20 USB Analog Surround 7.1:capture_1", "a3-engine:analog_1");
    EXPECT_TRUE (hasLine (show (alsa), "Scarlett inputs → engine")) << show (alsa);

    // On PipeWire the card is "system"; the Midi-Bridge port is what tells which card it is.
    EXPECT_TRUE (hasLine (show (inputs()), "Scarlett inputs → engine")) << show (inputs());

    // With no sign of a card, "system" stays what it is called.
    EXPECT_TRUE (hasLine (show (inputs(), 0, false), "system inputs → engine")) << show (inputs(), 0, false);
}

TEST (LinksOverview, LevelMeterFeedsAreCountedNotListed)
{
    Graph graph;
    graph.link ("a3-engine:main_sub", "beat-analyzer:vu_main_sub");
    graph.link ("a3-engine:main_top_1", "beat-analyzer:vu_main_top1");
    graph.link ("a3-engine:phones_L", "beat-analyzer:vu_phones_L");
    graph.link ("a3-engine:rec_L", "beat-analyzer:bpm_1");
    const auto text = show (graph);
    EXPECT_TRUE (hasLine (text, "  3 ports → VU meters (level meters)")) << text;
    EXPECT_TRUE (hasLine (text, "  rec L → bpm 1 (tempo)")) << text;
}

TEST (LinksOverview, GroupsRunInputsThenOutputsThenTheRest)
{
    Graph graph;
    graph.link ("StemDeck-A:cue_1", "a3-engine:deckA_cue_1");
    graph.link ("a3-engine:rec_L", "beat-analyzer:bpm_1");
    graph.link ("a3-engine:phones_L", "system:playback_9");
    graph.link ("system:capture_1", "a3-engine:analog_1");
    graph.link ("system:capture_9", "AES67 a3nuc1 out:in_1");
    const auto text = show (graph);
    const auto at = [&] (const char* heading) { return text.find (heading); };
    EXPECT_LT (at ("Scarlett inputs → engine"), at ("engine → Scarlett"));
    EXPECT_LT (at ("engine → Scarlett"), at ("engine → beat-analyzer"));
    EXPECT_LT (at ("engine → beat-analyzer"), at ("StemDeck A → engine"));
    EXPECT_LT (at ("StemDeck A → engine"), at ("Scarlett → AES67 a3nuc1 out"));
}

TEST (LinksOverview, TheLastLineCountsMissingTargetsAndPointsToTheStatusPage)
{
    EXPECT_TRUE (hasLine (show (inputs(), 10), "Not connected (configured, target missing): 10  → see the window's STATUS page"))
        << show (inputs(), 10);
    EXPECT_TRUE (hasLine (show (inputs(), 0), "Not connected (configured, target missing): none")) << show (inputs(), 0);
    const auto unknown = show (inputs(), std::nullopt);
    EXPECT_NE (unknown.find ("not counted"), std::string::npos) << unknown;
}

TEST (LinksOverview, WithoutTheEngineInTheGraphTheMissingCountIsNotTheNews)
{
    Graph graph;
    graph.link ("system:capture_9", "AES67 a3nuc1 out:in_1");
    const auto text = show (graph, 40);
    EXPECT_NE (text.find ("The engine is not running"), std::string::npos) << text;
    EXPECT_EQ (text.find ("Not connected"), std::string::npos) << text;
}

TEST (LinksOverview, NothingConnectedSaysSo)
{
    Graph graph;
    graph.ports = { "a3-engine:analog_1", "system:capture_1" };
    const auto text = show (graph);
    EXPECT_NE (text.find ("No connections"), std::string::npos) << text;
}
