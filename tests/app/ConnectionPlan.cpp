// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <algorithm>
#include <cerrno>
#include "ConnectionPlan.h"

namespace
{
using a3app::Link;
using Links = std::vector<Link>;
using Ports = std::vector<std::string>;

bool contains (const Links& links, const Link& link)
{
    return std::find (links.begin(), links.end(), link) != links.end();
}

// The graph of a small rig: the engine's ports, the card, the analyzer, StemDeck.
Ports rigPorts()
{
    return { "a3-engine:analog_1", "a3-engine:analog_2", "a3-engine:deckA_1", "a3-engine:main_top_1",
             "a3-engine:rec_L", "system:capture_1", "system:capture_2", "system:playback_1",
             "beat-analyzer:in_1", "StemDeck-A:out_1", "StemDeck-A:rec_in_L" };
}

a3app::IoMap rigMap()
{
    a3app::IoMap map;
    map.jack["analog_1"] = { "system:capture_1" };
    map.jack["main_top_1"] = { "system:playback_1" };
    map.jack["rec_L"] = { "StemDeck-A:rec_in_L" };
    return map;
}
}

namespace a3app
{
void PrintTo (const Link& link, std::ostream* os) { *os << link.from << " -> " << link.to; }
}

TEST (ConnectionPlan, MakesWhatTheMapSays)
{
    const auto plan = a3app::planConnections (rigMap(), {}, {}, rigPorts());
    // A peer feeds an input; an output feeds its peer.
    EXPECT_EQ (plan.add, (Links { { "system:capture_1", "a3-engine:analog_1" },
                                  { "a3-engine:main_top_1", "system:playback_1" },
                                  { "a3-engine:rec_L", "StemDeck-A:rec_in_L" } }));
    EXPECT_TRUE (plan.remove.empty());
    EXPECT_TRUE (plan.missingPeers.empty());

    // What is live already is not made twice, whoever made it.
    const Links live { { "system:capture_1", "a3-engine:analog_1" } };
    const auto again = a3app::planConnections (rigMap(), live, {}, rigPorts());
    EXPECT_EQ (again.add.size(), 2u);
    EXPECT_FALSE (contains (again.add, live[0]));

    // The client name is the engine's port prefix.
    const auto other = a3app::planConnections (rigMap(), {}, {}, { "eng:analog_1", "system:capture_1" }, "eng");
    EXPECT_EQ (other.add, (Links { { "system:capture_1", "eng:analog_1" } }));
}

TEST (ConnectionPlan, NeverRemovesAForeignLink)
{
    // StemDeck feeds an engine port the map does not name; the engine did not make it.
    const Links live { { "StemDeck-A:out_1", "a3-engine:deckA_1" },
                       { "system:capture_2", "a3-engine:analog_1" } };
    const auto plan = a3app::planConnections (rigMap(), live, {}, rigPorts());
    EXPECT_TRUE (plan.remove.empty());

    // Not even with an empty map.
    const auto empty = a3app::planConnections ({}, live, {}, rigPorts());
    EXPECT_TRUE (empty.remove.empty());
    EXPECT_TRUE (empty.add.empty());
}

TEST (ConnectionPlan, RemovesOnlyItsOwnStaleLinks)
{
    const Link own { "system:capture_1", "a3-engine:analog_1" };
    const Link foreign { "system:capture_2", "a3-engine:analog_2" };
    // The map moved analog_1 to capture_2.
    auto map = rigMap();
    map.jack["analog_1"] = { "system:capture_2" };
    const Links live { own, foreign, { "a3-engine:main_top_1", "system:playback_1" } };
    const Links made { own, { "a3-engine:main_top_1", "system:playback_1" } };

    const auto plan = a3app::planConnections (map, live, made, rigPorts());
    EXPECT_EQ (plan.remove, (Links { own }));
    EXPECT_TRUE (contains (plan.add, { "system:capture_2", "a3-engine:analog_1" }));
    EXPECT_FALSE (contains (plan.remove, foreign));

    // A link it made that is no longer live is not removed again.
    const auto gone = a3app::planConnections (map, { foreign }, made, rigPorts());
    EXPECT_TRUE (gone.remove.empty());
}

TEST (ConnectionPlan, ReMakesWhenAPeerReappears)
{
    // StemDeck restarts: its port is gone, the link with it.
    auto ports = rigPorts();
    ports.erase (std::find (ports.begin(), ports.end(), "StemDeck-A:rec_in_L"));
    const Links made { { "a3-engine:rec_L", "StemDeck-A:rec_in_L" } };
    const auto absent = a3app::planConnections (rigMap(), {}, a3app::stillOwned (made, {}), ports);
    EXPECT_EQ (absent.missingPeers, (Ports { "StemDeck-A:rec_in_L" }));
    EXPECT_FALSE (contains (absent.add, made[0]));
    EXPECT_TRUE (absent.remove.empty());

    // It is back: the link is made again, nothing is missing.
    const auto back = a3app::planConnections (rigMap(), {}, {}, rigPorts());
    EXPECT_TRUE (contains (back.add, made[0]));
    EXPECT_TRUE (back.missingPeers.empty());
}

TEST (ConnectionPlan, ALinkThatDiedIsNoLongerOwned)
{
    const Link a { "system:capture_1", "a3-engine:analog_1" }, b { "a3-engine:rec_L", "StemDeck-A:rec_in_L" };
    // b died with StemDeck's port; if anyone makes it again, it is theirs until the engine makes it.
    EXPECT_EQ (a3app::stillOwned ({ a, b }, { a, { "x:out", "y:in" } }), (Links { a }));
    EXPECT_TRUE (a3app::stillOwned ({}, { a }).empty());
}

TEST (ConnectionPlan, DirectLinksAreOwnedToo)
{
    // R4: system capture -> analyzer, made by the engine though neither port is its own.
    auto map = rigMap();
    map.directLinks = { { "system:capture_2", "beat-analyzer:in_1" } };
    const Link direct { "system:capture_2", "beat-analyzer:in_1" };

    const auto plan = a3app::planConnections (map, {}, {}, rigPorts());
    EXPECT_TRUE (contains (plan.add, direct));

    // The map drops it: the engine removes it, as it made it.
    const auto dropped = a3app::planConnections (rigMap(), { direct }, { direct }, rigPorts());
    EXPECT_EQ (dropped.remove, (Links { direct }));

    // The same link made by someone else stays.
    const auto foreign = a3app::planConnections (rigMap(), { direct }, {}, rigPorts());
    EXPECT_TRUE (foreign.remove.empty());

    // An absent end is a missing peer, not an error.
    map.directLinks = { { "system:capture_2", "aes67-send:in_1" } };
    EXPECT_EQ (a3app::planConnections (map, {}, {}, rigPorts()).missingPeers, (Ports { "aes67-send:in_1" }));
}

TEST (ConnectionPlan, MissingPeersAreNamedOnce)
{
    auto map = rigMap();
    map.jack["analog_2"] = { "aes67-recv:out_1" };
    map.jack["analog_1"] = { "system:capture_1", "aes67-recv:out_1" };
    const auto plan = a3app::planConnections (map, {}, {}, rigPorts());
    EXPECT_EQ (plan.missingPeers, (Ports { "aes67-recv:out_1" }));
}

TEST (ConnectionPlan, ClientNamesOfPorts)
{
    EXPECT_EQ (a3app::clientNamesOf ({ "system:capture_1", "REAPER:out1", "system:playback_1", "a:b:c" }),
               (Ports { "system", "REAPER", "a" }));
    EXPECT_TRUE (a3app::clientNamesOf ({}).empty());
}

TEST (JackBackend, RefusesBesideReaperUnlessNoConnect)
{
    const Ports withReaper { "system", "REAPER", "beat-analyzer" };
    const auto refused = a3app::refuseBesideReaper (withReaper, true);
    EXPECT_TRUE (refused.failed());
    EXPECT_EQ (refused.getErrorMessage(), "REAPER is running: start with --no-connect");

    EXPECT_TRUE (a3app::refuseBesideReaper (withReaper, false).wasOk());
    EXPECT_TRUE (a3app::refuseBesideReaper ({ "system", "StemDeck-A", "REAPER-bridge" }, true).wasOk());
    EXPECT_TRUE (a3app::refuseBesideReaper ({}, true).wasOk());
}

// R4: a link someone else made between the graph snapshot and jack_connect is theirs.
TEST (JackBackend, OnlyALinkItMadeIsOwned)
{
    using a3app::ConnectOutcome;
    EXPECT_EQ (a3app::connectOutcome (0), ConnectOutcome::owned);
    EXPECT_EQ (a3app::connectOutcome (EEXIST), ConnectOutcome::foreign);
    EXPECT_EQ (a3app::connectOutcome (-EEXIST), ConnectOutcome::foreign);
    EXPECT_EQ (a3app::connectOutcome (-1), ConnectOutcome::failed);
    EXPECT_EQ (a3app::connectOutcome (1), ConnectOutcome::failed);
}

// `a3-core --links` counts the configured links that have nowhere to land. A link whose missing end is the
// engine's own port is not one: that is "the engine is not running", said on its own.
TEST (ConnectionPlan, CountsLinksWhoseTargetIsMissing)
{
    EXPECT_EQ (a3app::linksWithMissingTarget (rigMap(), rigPorts()), 0u);

    Ports withoutCard = rigPorts();
    withoutCard.erase (std::remove (withoutCard.begin(), withoutCard.end(), "system:playback_1"), withoutCard.end());
    EXPECT_EQ (a3app::linksWithMissingTarget (rigMap(), withoutCard), 1u);   // main_top_1 -> playback_1

    auto direct = rigMap();
    direct.directLinks = { { "system:capture_9", "beat-analyzer:vu_a" }, { "system:capture_1", "beat-analyzer:in_1" } };
    EXPECT_EQ (a3app::linksWithMissingTarget (direct, rigPorts()), 1u);   // capture_9 is not there

    // No engine in the graph: its ports are not "missing targets".
    EXPECT_EQ (a3app::linksWithMissingTarget (rigMap(), { "system:capture_1", "system:playback_1" }), 1u);   // only StemDeck-A:rec_in_L
}
