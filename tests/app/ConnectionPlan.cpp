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

// The beat-analyzer listens to the REC bus's left side. Like the AES67 sender link it is built in (2026-10-30), not
// an I/O map entry: the REC row on the OUTPUT page stays free for an analog recorder.
TEST (BuiltInLinks, TheBeatAnalyzerIsAlwaysLinked)
{
    a3app::AppConfig config;
    config.io.jack["main_sub"] = { "system:playback_1" };
    const auto linked = a3app::linkMapFor (config, "a3nuc1");
    EXPECT_EQ (linked.jack.at ("rec_L"), (std::vector<std::string> { "beat-analyzer:bpm_1", "AES67 a3nuc1 out:send_CH1" }));
    EXPECT_EQ (config.io.jack.count ("rec_L"), 0u) << "never written to the file";

    config.io.jack["rec_L"] = { "system:playback_7" };   // an analog recorder on the REC row
    EXPECT_EQ (a3app::linkMapFor (config, "").jack.at ("rec_L"), (std::vector<std::string> { "system:playback_7", "beat-analyzer:bpm_1" }));
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

namespace
{
a3core::StreamSpec mainStream()
{
    a3core::StreamSpec spec;
    spec.key = "main";
    spec.session = "A3 Core MAIN";
    spec.channelNames = { "SUB", "T1", "T2", "T3" };
    spec.enginePorts = { "main_sub", "main_top_1", "main_top_2", "main_top_3" };
    return spec;
}

a3core::StreamSpec recStream()
{
    a3core::StreamSpec spec;
    spec.key = "rec";
    spec.session = "A3 Core REC";
    spec.channelNames = { "L", "R" };
    spec.enginePorts = { "rec_L", "rec_R" };
    return spec;
}

a3core::StreamSpec phonesStream()
{
    a3core::StreamSpec spec;
    spec.key = "phones";
    spec.session = "A3 Core PHONES";
    spec.channelNames = { "L", "R" };
    spec.enginePorts = { "phones_L", "phones_R" };
    return spec;
}

a3core::StreamSpec legacyStream()
{
    a3core::StreamSpec spec;
    spec.key = "legacy";
    spec.session = "AES67 a3nuc1 out";
    spec.legacy = true;
    spec.channelNames = { "CH1", "CH2" };
    return spec;
}
}

TEST (ConnectionPlan, MainSubAndTopsGoToTheMainSender)
{
    a3app::AppConfig config;
    const auto linked = a3app::linkMapFor (config, "a3nuc1", { legacyStream(), mainStream() });
    EXPECT_EQ (linked.jack.at ("main_sub"), std::vector<std::string> { "A3 Core MAIN:send_SUB" });
    EXPECT_EQ (linked.jack.at ("main_top_3"), std::vector<std::string> { "A3 Core MAIN:send_T3" });
}

TEST (ConnectionPlan, RecAndPhonesGoToTheirOwnStreams)
{
    a3app::AppConfig config;
    const auto linked = a3app::linkMapFor (config, "a3nuc1", { legacyStream(), recStream(), phonesStream() });
    EXPECT_TRUE (links (linked.jack.at ("rec_L"), "AES67 a3nuc1 out:send_CH1")) << "the legacy link stays";
    EXPECT_TRUE (links (linked.jack.at ("rec_L"), "A3 Core REC:send_L"));
    EXPECT_TRUE (links (linked.jack.at ("rec_R"), "A3 Core REC:send_R"));
    EXPECT_EQ (linked.jack.at ("phones_R"), std::vector<std::string> { "A3 Core PHONES:send_R" });
}

TEST (ConnectionPlan, NoSpecsMeansTheOldMap)
{
    a3app::AppConfig config;
    config.io.jack["main_top_1"] = { "system:playback_1" };
    EXPECT_EQ (a3app::linkMapFor (config, "a3nuc1", {}).jack, a3app::linkMapFor (config, "a3nuc1").jack);
    EXPECT_EQ (a3app::linkMapFor (config, "a3nuc1", { legacyStream() }).jack, a3app::linkMapFor (config, "a3nuc1").jack)
        << "the legacy stream adds nothing: its link is addRecToAes67Sender's";
}

TEST (ConnectionPlan, EachLinkIsMadeOnce)
{
    a3app::AppConfig config;
    config.io.jack["main_sub"] = { "A3 Core MAIN:send_SUB" };
    const auto streams = std::vector<a3core::StreamSpec> { mainStream(), mainStream() };
    const auto linked = a3app::linkMapFor (config, "a3nuc1", streams);
    EXPECT_EQ (linked.jack.at ("main_sub"), std::vector<std::string> { "A3 Core MAIN:send_SUB" });
    EXPECT_EQ (linked.jack.at ("main_top_1"), std::vector<std::string> { "A3 Core MAIN:send_T1" });
}
