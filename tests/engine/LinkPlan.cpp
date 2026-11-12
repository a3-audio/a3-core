// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <algorithm>
#include <cerrno>
#include "a3-links/LinkPlan.h"

namespace
{
using a3::links::Link;
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

a3::links::LinkMap rigMap()
{
    a3::links::LinkMap map;
    map.jack["analog_1"] = { "system:capture_1" };
    map.jack["main_top_1"] = { "system:playback_1" };
    map.jack["rec_L"] = { "StemDeck-A:rec_in_L" };
    return map;
}

// The ports of rigPorts() that take audio in; the direction of a link follows from this.
const Ports rigInputs { "analog_1", "analog_2", "deckA_1" };

a3::links::ConnectionPlan planWith (const a3::links::LinkMap& map, const Links& live, const Links& made,
                                    const Ports& ports, const std::string& client = "a3-engine")
{
    return a3::links::planConnections (map, live, made, ports, rigInputs, client);
}
}

namespace a3::links
{
void PrintTo (const Link& link, std::ostream* os) { *os << link.from << " -> " << link.to; }
}

TEST (LinkPlan, MakesWhatTheMapSays)
{
    const auto plan = planWith (rigMap(), {}, {}, rigPorts());
    // A peer feeds an input; an output feeds its peer.
    EXPECT_EQ (plan.add, (Links { { "system:capture_1", "a3-engine:analog_1" },
                                  { "a3-engine:main_top_1", "system:playback_1" },
                                  { "a3-engine:rec_L", "StemDeck-A:rec_in_L" } }));
    EXPECT_TRUE (plan.remove.empty());
    EXPECT_TRUE (plan.missingPeers.empty());

    // What is live already is not made twice, whoever made it.
    const Links live { { "system:capture_1", "a3-engine:analog_1" } };
    const auto again = planWith (rigMap(), live, {}, rigPorts());
    EXPECT_EQ (again.add.size(), 2u);
    EXPECT_FALSE (contains (again.add, live[0]));

    // The client name is the engine's port prefix.
    const auto other = planWith (rigMap(), {}, {}, { "eng:analog_1", "system:capture_1" }, "eng");
    EXPECT_EQ (other.add, (Links { { "system:capture_1", "eng:analog_1" } }));
}

// The engine owns every link between its ports and the device, AES67 or any other port it plans (2026-10-30: a
// WirePlumber restart pointed system:* at "AES67 deck C" and the sender, and those links stayed). Only clients
// that make their own links to the engine (StemDeck-<X>, beat-analyzer) keep theirs.
TEST (LinkPlan, RemovesUnplannedLinksOnItsOwnPorts)
{
    const Link wrongDevice { "AES67 deck C:receive_MAIN1", "a3-engine:analog_1" };
    const Link wrongSender { "a3-engine:main_top_1", "AES67 a3nuc1 out:send_CH2" };
    const Link wrongChannel { "system:capture_2", "a3-engine:analog_1" };
    const Links live { wrongDevice, wrongSender, wrongChannel, { "system:capture_1", "a3-engine:analog_1" } };
    const auto plan = planWith (rigMap(), live, {}, rigPorts());
    EXPECT_EQ (plan.remove, (Links { wrongDevice, wrongSender, wrongChannel }));

    // Links between two ports that are not the engine's are none of its business.
    const auto others = planWith (rigMap(), { { "system:capture_2", "beat-analyzer:in_1" } }, {}, rigPorts());
    EXPECT_TRUE (others.remove.empty());
}

TEST (LinkPlan, KeepsTheLinksOfClientsThatLinkThemselves)
{
    // StemDeck links its recorder from the engine and may feed a slot the map does not name; the analyzer's beat input.
    const Links live { { "StemDeck-A:out_1", "a3-engine:deckA_1" },
                       { "a3-engine:rec_L", "StemDeck-A:rec_L" },
                       { "a3-engine:rec_L", "beat-analyzer:bpm_1" } };
    EXPECT_TRUE (planWith (rigMap(), live, {}, rigPorts()).remove.empty());
    EXPECT_TRUE (planWith ({}, live, {}, rigPorts()).remove.empty());
    EXPECT_TRUE (a3::links::linksItself ("StemDeck-B:rec_R"));
    EXPECT_TRUE (a3::links::linksItself ("beat-analyzer:bpm_1"));
    EXPECT_FALSE (a3::links::linksItself ("AES67 deck C:receive_MAIN1"));
    EXPECT_FALSE (a3::links::linksItself ("system:capture_1"));
}

TEST (LinkPlan, RemovesOnlyItsOwnStaleLinks)
{
    const Link own { "system:capture_1", "a3-engine:analog_1" };
    const Link foreign { "StemDeck-A:out_1", "a3-engine:analog_2" };
    // The map moved analog_1 to capture_2.
    auto map = rigMap();
    map.jack["analog_1"] = { "system:capture_2" };
    const Links live { own, foreign, { "a3-engine:main_top_1", "system:playback_1" } };
    const Links made { own, { "a3-engine:main_top_1", "system:playback_1" } };

    const auto plan = planWith (map, live, made, rigPorts());
    EXPECT_EQ (plan.remove, (Links { own }));
    EXPECT_TRUE (contains (plan.add, { "system:capture_2", "a3-engine:analog_1" }));
    EXPECT_FALSE (contains (plan.remove, foreign));

    // A link it made that is no longer live is not removed again.
    const auto gone = planWith (map, { foreign }, made, rigPorts());
    EXPECT_TRUE (gone.remove.empty());
}

TEST (LinkPlan, ReMakesWhenAPeerReappears)
{
    // StemDeck restarts: its port is gone, the link with it.
    auto ports = rigPorts();
    ports.erase (std::find (ports.begin(), ports.end(), "StemDeck-A:rec_in_L"));
    const Links made { { "a3-engine:rec_L", "StemDeck-A:rec_in_L" } };
    const auto absent = planWith (rigMap(), {}, a3::links::stillOwned (made, {}), ports);
    EXPECT_EQ (absent.missingPeers, (Ports { "StemDeck-A:rec_in_L" }));
    EXPECT_FALSE (contains (absent.add, made[0]));
    EXPECT_TRUE (absent.remove.empty());

    // It is back: the link is made again, nothing is missing.
    const auto back = planWith (rigMap(), {}, {}, rigPorts());
    EXPECT_TRUE (contains (back.add, made[0]));
    EXPECT_TRUE (back.missingPeers.empty());
}

TEST (LinkPlan, ALinkThatDiedIsNoLongerOwned)
{
    const Link a { "system:capture_1", "a3-engine:analog_1" }, b { "a3-engine:rec_L", "StemDeck-A:rec_in_L" };
    // b died with StemDeck's port; if anyone makes it again, it is theirs until the engine makes it.
    EXPECT_EQ (a3::links::stillOwned ({ a, b }, { a, { "x:out", "y:in" } }), (Links { a }));
    EXPECT_TRUE (a3::links::stillOwned ({}, { a }).empty());
}

TEST (LinkPlan, DirectLinksAreOwnedToo)
{
    // R4: system capture -> analyzer, made by the engine though neither port is its own.
    auto map = rigMap();
    map.directLinks = { { "system:capture_2", "beat-analyzer:in_1" } };
    const Link direct { "system:capture_2", "beat-analyzer:in_1" };

    const auto plan = planWith (map, {}, {}, rigPorts());
    EXPECT_TRUE (contains (plan.add, direct));

    // The map drops it: the engine removes it, as it made it.
    const auto dropped = planWith (rigMap(), { direct }, { direct }, rigPorts());
    EXPECT_EQ (dropped.remove, (Links { direct }));

    // The same link made by someone else stays.
    const auto foreign = planWith (rigMap(), { direct }, {}, rigPorts());
    EXPECT_TRUE (foreign.remove.empty());

    // An absent end is a missing peer, not an error.
    map.directLinks = { { "system:capture_2", "aes67-send:in_1" } };
    EXPECT_EQ (planWith (map, {}, {}, rigPorts()).missingPeers, (Ports { "aes67-send:in_1" }));
}

TEST (LinkPlan, MissingPeersAreNamedOnce)
{
    auto map = rigMap();
    map.jack["analog_2"] = { "aes67-recv:out_1" };
    map.jack["analog_1"] = { "system:capture_1", "aes67-recv:out_1" };
    const auto plan = planWith (map, {}, {}, rigPorts());
    EXPECT_EQ (plan.missingPeers, (Ports { "aes67-recv:out_1" }));
}

TEST (LinkPlan, ClientNamesOfPorts)
{
    EXPECT_EQ (a3::links::clientNamesOf ({ "system:capture_1", "REAPER:out1", "system:playback_1", "a:b:c" }),
               (Ports { "system", "REAPER", "a" }));
    EXPECT_TRUE (a3::links::clientNamesOf ({}).empty());
}

// R4: a link someone else made between the graph snapshot and jack_connect is theirs.
TEST (LinkPlan, OnlyALinkItMadeIsOwned)
{
    using a3::links::ConnectOutcome;
    EXPECT_EQ (a3::links::connectOutcome (0), ConnectOutcome::owned);
    EXPECT_EQ (a3::links::connectOutcome (EEXIST), ConnectOutcome::foreign);
    EXPECT_EQ (a3::links::connectOutcome (-EEXIST), ConnectOutcome::foreign);
    EXPECT_EQ (a3::links::connectOutcome (-1), ConnectOutcome::failed);
    EXPECT_EQ (a3::links::connectOutcome (1), ConnectOutcome::failed);
}

TEST (LinkPlan, KnowsWhichOwnPortsTakeAudioIn)
{
    EXPECT_TRUE (a3::links::isInputPort ("analog_1", rigInputs));
    EXPECT_FALSE (a3::links::isInputPort ("main_top_1", rigInputs));
}

// `a3-core --links` counts the configured links that have nowhere to land. A link whose missing end is our
// own port is not one: that is "the engine is not running", said on its own.
TEST (LinkPlan, CountsLinksWhoseTargetIsMissing)
{
    EXPECT_EQ (a3::links::linksWithMissingTarget (rigMap(), rigPorts()), 0u);

    Ports withoutCard = rigPorts();
    withoutCard.erase (std::remove (withoutCard.begin(), withoutCard.end(), "system:playback_1"), withoutCard.end());
    EXPECT_EQ (a3::links::linksWithMissingTarget (rigMap(), withoutCard), 1u);   // main_top_1 -> playback_1

    auto direct = rigMap();
    direct.directLinks = { { "system:capture_9", "beat-analyzer:vu_a" }, { "system:capture_1", "beat-analyzer:in_1" } };
    EXPECT_EQ (a3::links::linksWithMissingTarget (direct, rigPorts()), 1u);   // capture_9 is not there

    // No engine in the graph: its ports are not "missing targets".
    EXPECT_EQ (a3::links::linksWithMissingTarget (rigMap(), { "system:capture_1", "system:playback_1" }), 1u);   // only StemDeck-A:rec_in_L
}

// pipewire-jack names a node "<name>-<id>" in a client's view when it appears while another node of that name still
// exists, and keeps that name after the old one is gone (pipewire-jack.c 1.6.9, :3873). An AES67 receiver that SAP
// recreates is such a node: deck C's stream came back as "AES67 deck C-633" in the engine's view only.
TEST (LinkPlan, LinksAPeerThatPipewireJackRenamed)
{
    a3::links::LinkMap map;
    map.jack["deckA_1"] = { "AES67 deck C:receive_MAIN1" };
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C-633:receive_MAIN1");

    const auto plan = planWith (map, {}, {}, ports);

    EXPECT_EQ (plan.add, (Links { { "AES67 deck C-633:receive_MAIN1", "a3-engine:deckA_1" } }));
    EXPECT_TRUE (plan.missingPeers.empty());
}

TEST (LinkPlan, KeepsTheLinkItMadeToARenamedPeer)
{
    a3::links::LinkMap map;
    map.jack["deckA_1"] = { "AES67 deck C:receive_MAIN1" };
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C-633:receive_MAIN1");
    const Links live { { "AES67 deck C-633:receive_MAIN1", "a3-engine:deckA_1" } };

    const auto plan = planWith (map, live, live, ports);

    EXPECT_TRUE (plan.add.empty());
    EXPECT_TRUE (plan.remove.empty());
}

// While the old node and its renamed successor both exist and no ages are known, the exact name is the one to link;
// the renamed one takes over once the old one is gone.
TEST (LinkPlan, PrefersTheExactNameOverARenamedOne)
{
    a3::links::LinkMap map;
    map.jack["deckA_1"] = { "AES67 deck C:receive_MAIN1" };
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C:receive_MAIN1");
    ports.push_back ("AES67 deck C-633:receive_MAIN1");

    const auto plan = planWith (map, {}, {}, ports);

    EXPECT_EQ (plan.add, (Links { { "AES67 deck C:receive_MAIN1", "a3-engine:deckA_1" } }));
}

// Several renamed nodes at once and no ages known: the highest id is the best guess (see the aged tests below).
TEST (LinkPlan, TakesTheNewestOfSeveralRenamedPeers)
{
    a3::links::LinkMap map;
    map.jack["deckA_1"] = { "AES67 deck C:receive_MAIN1" };
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C-633:receive_MAIN1");
    ports.push_back ("AES67 deck C-1201:receive_MAIN1");
    ports.push_back ("AES67 deck C-98:receive_MAIN1");

    const auto plan = planWith (map, {}, {}, ports);

    EXPECT_EQ (plan.add, (Links { { "AES67 deck C-1201:receive_MAIN1", "a3-engine:deckA_1" } }));
}

// Only "-<digits>" right before the colon is pipewire-jack's suffix; another client or another port is not the peer.
TEST (LinkPlan, IgnoresNamesThatAreNotARename)
{
    a3::links::LinkMap map;
    map.jack["deckA_1"] = { "AES67 deck C:receive_MAIN1" };
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C-x1:receive_MAIN1");
    ports.push_back ("AES67 deck C-633:receive_MAIN10");
    ports.push_back ("AES67 deck CD-633:receive_MAIN1");

    const auto plan = planWith (map, {}, {}, ports);

    EXPECT_TRUE (plan.add.empty());
    EXPECT_EQ (plan.missingPeers, (Ports { "AES67 deck C:receive_MAIN1" }));
}

TEST (LinkPlan, CountsARenamedPeerAsPresent)
{
    a3::links::LinkMap map;
    map.jack["deckA_1"] = { "AES67 deck C:receive_MAIN1" };
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C-633:receive_MAIN1");

    EXPECT_EQ (a3::links::linksWithMissingTarget (map, ports), 0u);
}

// The suffix pipewire-jack appends is the node's global id, and PipeWire reuses freed ids: a newer node can carry
// a lower suffix, and an old node can carry the plain name while its successor is renamed or the other way round.
// What orders them is the port's serial (jack_port_uuid, never reused); with it the newest port of a name wins.
namespace
{
a3::links::ConnectionPlan planAged (const a3::links::LinkMap& map, const Links& live, const Links& made,
                                    const Ports& ports, const a3::links::PortAges& ages)
{
    return a3::links::planConnections (map, live, made, ports, rigInputs, "a3-engine", ages);
}

a3::links::LinkMap deckCMap()
{
    a3::links::LinkMap map;
    map.jack["deckA_1"] = { "AES67 deck C:receive_MAIN1" };
    return map;
}
}

// SAP brings deck C back while the old, silent node still exists: the new one is renamed and is the one to link.
TEST (LinkPlan, LinksTheRenamedPeerWhenItIsNewerThanTheOldOne)
{
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C:receive_MAIN1");
    ports.push_back ("AES67 deck C-633:receive_MAIN1");
    const a3::links::PortAges ages { { "AES67 deck C:receive_MAIN1", 500 }, { "AES67 deck C-633:receive_MAIN1", 900 } };

    const auto plan = planAged (deckCMap(), {}, {}, ports, ages);

    EXPECT_EQ (plan.add, (Links { { "AES67 deck C-633:receive_MAIN1", "a3-engine:deckA_1" } }));
}

// Recreated again once the renamed one was alone: the newest node got the plain name back, the renamed one is stale.
TEST (LinkPlan, LinksThePlainNameWhenItIsNewerThanARenamedOne)
{
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C-633:receive_MAIN1");
    ports.push_back ("AES67 deck C:receive_MAIN1");
    const a3::links::PortAges ages { { "AES67 deck C-633:receive_MAIN1", 900 }, { "AES67 deck C:receive_MAIN1", 1400 } };

    const auto plan = planAged (deckCMap(), {}, {}, ports, ages);

    EXPECT_EQ (plan.add, (Links { { "AES67 deck C:receive_MAIN1", "a3-engine:deckA_1" } }));
}

// A reused id: the newest of two renamed nodes has the lower suffix.
TEST (LinkPlan, TakesTheNewestRenamedPeerByAgeNotBySuffix)
{
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C-633:receive_MAIN1");
    ports.push_back ("AES67 deck C-98:receive_MAIN1");
    const a3::links::PortAges ages { { "AES67 deck C-633:receive_MAIN1", 900 }, { "AES67 deck C-98:receive_MAIN1", 1200 } };

    const auto plan = planAged (deckCMap(), {}, {}, ports, ages);

    EXPECT_EQ (plan.add, (Links { { "AES67 deck C-98:receive_MAIN1", "a3-engine:deckA_1" } }));
}

// The link it made to the old node moves to the new one: one link on the port, never both.
TEST (LinkPlan, MovesItsLinkFromTheOldNodeToTheNewOne)
{
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C:receive_MAIN1");
    ports.push_back ("AES67 deck C-633:receive_MAIN1");
    const a3::links::PortAges ages { { "AES67 deck C:receive_MAIN1", 500 }, { "AES67 deck C-633:receive_MAIN1", 900 } };
    const Links live { { "AES67 deck C:receive_MAIN1", "a3-engine:deckA_1" } };

    const auto plan = planAged (deckCMap(), live, live, ports, ages);

    EXPECT_EQ (plan.add, (Links { { "AES67 deck C-633:receive_MAIN1", "a3-engine:deckA_1" } }));
    EXPECT_EQ (plan.remove, live);
}

// Deck B's rename is not deck C's peer, however new it is.
TEST (LinkPlan, ANewerRenameOfAnotherDeckIsNotThePeer)
{
    auto ports = rigPorts();
    ports.push_back ("AES67 deck C:receive_MAIN1");
    ports.push_back ("AES67 deck B-12:receive_MAIN1");
    const a3::links::PortAges ages { { "AES67 deck C:receive_MAIN1", 500 }, { "AES67 deck B-12:receive_MAIN1", 900 } };

    const auto plan = planAged (deckCMap(), {}, {}, ports, ages);

    EXPECT_EQ (plan.add, (Links { { "AES67 deck C:receive_MAIN1", "a3-engine:deckA_1" } }));
}

// An input with several peers (the card and a deck's stereo master): the peers that are in the graph are linked, the
// absent one is skipped without a fault and named as missing.
TEST (LinkPlan, AnAbsentPeerOnASharedInputIsSkippedTheOthersAreLinked)
{
    a3::links::LinkMap map;
    map.jack["analog_1"] = { "system:capture_1", "StemDeck-A:master_L" };
    const auto plan = planWith (map, {}, {}, rigPorts());
    EXPECT_EQ (plan.add, (Links { { "system:capture_1", "a3-engine:analog_1" } }));
    EXPECT_TRUE (plan.remove.empty());
    EXPECT_EQ (plan.missingPeers, (Ports { "StemDeck-A:master_L" }));

    // The deck appears: it is added beside the card, the card's link stays.
    Ports withDeck = rigPorts();
    withDeck.push_back ("StemDeck-A:master_L");
    const Link card { "system:capture_1", "a3-engine:analog_1" };
    const auto joined = planWith (map, { card }, { card }, withDeck);
    EXPECT_EQ (joined.add, (Links { { "StemDeck-A:master_L", "a3-engine:analog_1" } }));
    EXPECT_TRUE (joined.remove.empty());
    EXPECT_TRUE (joined.missingPeers.empty());
}
