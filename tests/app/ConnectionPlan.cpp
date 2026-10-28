// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "ConfigFile.h"
#include "ConnectionPlan.h"
#include "a3-links/LinkPlan.h"
#include <algorithm>

TEST (JackBackend, RefusesBesideReaperUnlessNoConnect)
{
    const std::vector<std::string> withReaper { "system", "REAPER", "beat-analyzer" };
    const auto refused = a3app::refuseBesideReaper (withReaper, true);
    EXPECT_TRUE (refused.failed());
    EXPECT_EQ (refused.getErrorMessage(), "REAPER is running: start with --no-connect");

    EXPECT_TRUE (a3app::refuseBesideReaper (withReaper, false).wasOk());
    EXPECT_TRUE (a3app::refuseBesideReaper ({ "system", "StemDeck-A", "REAPER-bridge" }, true).wasOk());
    EXPECT_TRUE (a3app::refuseBesideReaper ({}, true).wasOk());
}

namespace
{
bool links (const std::vector<std::string>& peers, const std::string& peer)
{
    return std::find (peers.begin(), peers.end(), peer) != peers.end();
}

// An engine.json saved before M10: the REC bus went to the single-instance StemDeck only.
a3::links::LinkMap savedBeforeM10()
{
    a3::links::LinkMap map;
    map.jack["rec_L"] = { "StemDeck:rec_L", "beat-analyzer:bpm_1" };
    map.jack["rec_R"] = { "StemDeck:rec_R" };
    map.jack["main_top_1"] = { "system:playback_1" };
    return map;
}
}

// Configuration 4 (P40): the Core's REC bus reaches its AES67 sender, so a deck host records it,
// even when the saved map predates M10.
TEST (BuiltInLinks, Configuration4RecBusReachesTheAes67SenderFromAPreM10SavedMap)
{
    auto map = savedBeforeM10();
    a3app::addRecToAes67Sender (map, "a3nuc1");

    EXPECT_TRUE (links (map.jack.at ("rec_L"), "AES67 a3nuc1 out:send_CH1"));
    EXPECT_TRUE (links (map.jack.at ("rec_R"), "AES67 a3nuc1 out:send_CH2"));
    // The saved entries stay as saved.
    EXPECT_TRUE (links (map.jack.at ("rec_L"), "StemDeck:rec_L"));
    EXPECT_TRUE (links (map.jack.at ("rec_L"), "beat-analyzer:bpm_1"));
    EXPECT_EQ (map.jack.at ("main_top_1"), std::vector<std::string> { "system:playback_1" });

    const std::vector<std::string> graph { "a3-engine:rec_L", "a3-engine:rec_R", "AES67 a3nuc1 out:send_CH1",
                                           "AES67 a3nuc1 out:send_CH2" };
    const auto plan = a3::links::planConnections (map, {}, {}, graph, { "analog_1" });
    EXPECT_TRUE (std::find (plan.add.begin(), plan.add.end(), a3::links::Link { "a3-engine:rec_L", "AES67 a3nuc1 out:send_CH1" }) != plan.add.end());
    EXPECT_TRUE (std::find (plan.add.begin(), plan.add.end(), a3::links::Link { "a3-engine:rec_R", "AES67 a3nuc1 out:send_CH2" }) != plan.add.end());
}

TEST (BuiltInLinks, TheSenderIsNamedAfterThisMachine)
{
    a3::links::LinkMap map;
    a3app::addRecToAes67Sender (map, "a3nuc9");
    EXPECT_EQ (map.jack.at ("rec_L"), std::vector<std::string> { "AES67 a3nuc9 out:send_CH1" });
    EXPECT_EQ (map.jack.at ("rec_R"), std::vector<std::string> { "AES67 a3nuc9 out:send_CH2" });
}

TEST (BuiltInLinks, AMapThatHasThemAlreadyIsUnchanged)
{
    a3::links::LinkMap map;
    map.jack["rec_L"] = { "AES67 a3nuc1 out:send_CH1" };
    map.jack["rec_R"] = { "AES67 a3nuc1 out:send_CH2" };
    const auto before = map.jack;
    a3app::addRecToAes67Sender (map, "a3nuc1");
    EXPECT_EQ (map.jack, before);
}

TEST (BuiltInLinks, NoHostNameNoLink)
{
    auto map = savedBeforeM10();
    const auto before = map.jack;
    a3app::addRecToAes67Sender (map, "");
    EXPECT_EQ (map.jack, before);
}

// The map the engine links (JackBackend) and counts (a3-core links): the loaded map plus the built-in
// links; the loaded config itself stays as loaded, so a Save never writes them.
TEST (BuiltInLinks, LinkMapForIsTheLoadedMapPlusTheBuiltInLinks)
{
    a3app::AppConfig config;
    static_cast<a3::links::LinkMap&> (config.io) = savedBeforeM10();
    config.io.directLinks = { { "system:capture_1", "beat-analyzer:in_1" } };
    config.io.deviceChannels["rec_L"] = 3;
    const auto loaded = config.io.jack;

    const auto linked = a3app::linkMapFor (config, "a3nuc1");

    EXPECT_EQ (linked.jack.at ("rec_L"), (std::vector<std::string> { "StemDeck:rec_L", "beat-analyzer:bpm_1", "AES67 a3nuc1 out:send_CH1" }));
    EXPECT_EQ (linked.jack.at ("rec_R"), (std::vector<std::string> { "StemDeck:rec_R", "AES67 a3nuc1 out:send_CH2" }));
    EXPECT_EQ (linked.jack.at ("main_top_1"), std::vector<std::string> { "system:playback_1" });
    EXPECT_EQ (linked.directLinks, config.io.directLinks);
    EXPECT_EQ (linked.deviceChannels, config.io.deviceChannels);
    EXPECT_EQ (config.io.jack, loaded);
}

// F1 finding 3: a deck host can boot after the Core, and a3-aes67 can restart. The engine applies
// its map again when a port registers anywhere in the graph, and once a second as a fallback, so
// "AES67 deck B:receive_*" appearing late is linked without restarting the engine.
TEST (LinkPlanDue, APortRegistrationOrASecondAppliesTheMapAgain)
{
    EXPECT_TRUE (a3app::linkPlanDue (true, false, 0.0, -1.0e9)) << "the first service after open";
    EXPECT_TRUE (a3app::linkPlanDue (true, true, 10.1, 10.0)) << "a port came or went: at once";
    EXPECT_FALSE (a3app::linkPlanDue (true, false, 10.5, 10.0)) << "nothing changed within the second";
    EXPECT_TRUE (a3app::linkPlanDue (true, false, 11.0, 10.0)) << "the periodic fallback, a missed callback heals";
    EXPECT_FALSE (a3app::linkPlanDue (false, true, 11.0, 10.0)) << "--no-connect never links";
}
