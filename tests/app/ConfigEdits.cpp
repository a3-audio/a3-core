// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <gtest/gtest.h>
#include "ConfigEdits.h"
#include "EngineFixture.h"
#include "SpatialTestData.h"

namespace
{
a3app::AppConfig sample()
{
    a3app::AppConfig config;
    config.engine = a3test::engineConfig (4, 2);
    config.spatialDirectory = a3test::shippedSpatialDirectory;
    config.io.jack["analog_1"] = { "system:capture_1" };
    config.io.jack["ch4_post_L"] = { "other:in_1" };
    config.io.deviceChannels["ch4_pre_R"] = 5;
    config.io.directLinks = { { "system:capture_9", "analyzer:in_1" } };
    return config;
}
}

TEST (ConfigEdits, ChannelsFollowTheAnalogPairsAndPruneTheMap)
{
    juce::String error;
    const auto smaller = a3app::withChannels (sample(), 2, error);
    ASSERT_TRUE (smaller) << error;
    EXPECT_EQ (smaller->engine.channels, 2);
    EXPECT_EQ (smaller->engine.analogPairs, (std::vector<int> { 1, 2 }));
    EXPECT_EQ (smaller->io.jack.count ("ch4_post_L"), 0u);       // channel 4 is gone
    EXPECT_EQ (smaller->io.deviceChannels.count ("ch4_pre_R"), 0u);
    EXPECT_EQ (smaller->io.jack.count ("analog_1"), 1u);
    EXPECT_EQ (smaller->io.directLinks.size(), 1u);

    const auto bigger = a3app::withChannels (sample(), 8, error);
    ASSERT_TRUE (bigger) << error;
    EXPECT_EQ (bigger->engine.analogPairs, (std::vector<int> { 1, 2, 3, 4, 5, 0, 0, 0 }));   // pair 6 is the return

    EXPECT_FALSE (a3app::withChannels (sample(), 0, error));
    EXPECT_NE (error, juce::String());
}

TEST (ConfigEdits, EveryEditSurvivesTheLoadersChecks)
{
    juce::String error;
    const auto edited = a3app::withChannels (sample(), 9, error);
    ASSERT_TRUE (edited);
    const auto file = juce::File::createTempFile ("a3-edit");
    ASSERT_TRUE (a3app::saveAppConfig (*edited, file).wasOk());
    EXPECT_TRUE (a3app::loadAppConfig (file, error)) << error;
    file.deleteFile();
}

TEST (ConfigEdits, APortTheLayoutLostLeavesTheMap)
{
    juce::String error;
    auto config = sample();
    config.io.jack["main_top_4"] = { "x:y" };
    config.io.deviceChannels["main_top_4"] = 2;
    auto ports = *a3app::portNamesFor (config, error);
    ports.outputs.erase (std::remove (ports.outputs.begin(), ports.outputs.end(), "main_top_4"), ports.outputs.end());
    const auto pruned = a3app::prunedTo (config.io, ports);
    EXPECT_EQ (pruned.jack.count ("main_top_4"), 0u);
    EXPECT_EQ (pruned.deviceChannels.count ("main_top_4"), 0u);
    EXPECT_EQ (pruned.jack.count ("analog_1"), 1u);

    // A directory that loads keeps a valid map; one that does not is refused with its name.
    a3test::ScratchDirectory copy;
    const auto moved = a3app::withSpatialDirectory (sample(), copy.root(), error);
    ASSERT_TRUE (moved) << error;
    EXPECT_EQ (moved->spatialDirectory, copy.root());
    EXPECT_FALSE (a3app::withSpatialDirectory (sample(), juce::File ("/nonexistent/spatial"), error));
    EXPECT_NE (error.indexOf ("/nonexistent/spatial"), -1);
}

TEST (ConfigEdits, TrimsAreBoundedAndAFirstSetOpensAZone)
{
    juce::String error;
    auto config = sample();
    config.engine.trims.main.reset();
    const auto set = a3app::withTrim (config, a3app::TrimZone::main, -3.5f, error);
    ASSERT_TRUE (set) << error;
    EXPECT_EQ (set->engine.trims.main, std::optional<float> (-3.5f));
    EXPECT_TRUE (a3app::withTrim (config, a3app::TrimZone::rec, -90.0f, error));
    EXPECT_TRUE (a3app::withTrim (config, a3app::TrimZone::phones, 6.0f, error));
    for (const float bad : { 6.5f, -91.0f, (float) NAN, (float) INFINITY })
        EXPECT_FALSE (a3app::withTrim (config, a3app::TrimZone::booth, bad, error)) << bad;
}

TEST (ConfigEdits, SameConfigSeesEveryField)
{
    auto a = sample(), b = sample();
    EXPECT_TRUE (a3app::sameConfig (a, b));
    b.io.deviceChannels["analog_2"] = 1;
    EXPECT_FALSE (a3app::sameConfig (a, b));
    b = sample(); b.backend.blockSize = 256;
    EXPECT_FALSE (a3app::sameConfig (a, b));
    b = sample(); b.engine.filterKind = a3::FilterKind::ladder;
    EXPECT_FALSE (a3app::sameConfig (a, b));
    b = sample(); b.spatialDirectory = a3test::reaperBoothSpatialDirectory;
    EXPECT_FALSE (a3app::sameConfig (a, b));
}

TEST (ConfigEdits, IoRowsSeparateMappedFromForeignPeers)
{
    juce::String error;
    const auto config = sample();
    const auto ports = *a3app::portNamesFor (config, error);
    const a3app::LiveLinks links {
        { "system:capture_1", "a3-engine:analog_1" },     // the mapped one
        { "someone:out", "a3-engine:analog_1" },          // foreign
        { "a3-engine:ch4_post_L", "other:in_1" },         // mapped output
        { "a3-engine:ch4_post_L", "recorder:in" } };      // foreign
    const auto rows = a3app::ioRows (config, ports, links);
    ASSERT_EQ (rows.size(), ports.inputs.size() + ports.outputs.size());
    const auto find = [&] (const std::string& port) -> const a3app::IoRow& { for (auto& r : rows) if (r.port == port) return r; throw std::runtime_error (port); };
    EXPECT_TRUE (find ("analog_1").input);
    EXPECT_EQ (find ("analog_1").mapped, (std::vector<std::string> { "system:capture_1" }));
    EXPECT_EQ (find ("analog_1").foreign, (std::vector<std::string> { "someone:out" }));
    EXPECT_FALSE (find ("ch4_post_L").input);
    EXPECT_EQ (find ("ch4_post_L").foreign, (std::vector<std::string> { "recorder:in" }));
    EXPECT_EQ (find ("ch4_pre_R").deviceChannel, std::optional<int> (5));
    EXPECT_FALSE (find ("analog_2").deviceChannel);
}

TEST (ConfigEdits, SaveRefusesWhatTheLoaderWouldRefuse)
{
    juce::String error;
    const auto file = juce::File::createTempFile ("a3-save");
    const auto good = sample();
    ASSERT_TRUE (a3app::saveAppConfig (good, file).wasOk());
    const auto before = file.loadFileAsString();

    auto bad = sample();
    bad.engine.trims.main = 10.0f;   // above +6 dB
    const auto result = a3app::saveAppConfig (bad, file);
    EXPECT_TRUE (result.failed());
    EXPECT_NE (result.getErrorMessage().indexOf ("trimsDb"), -1);
    EXPECT_EQ (file.loadFileAsString(), before);   // the file on disk is the good one, untouched

    bad = sample();
    bad.io.jack["no_such_port"] = { "a:b" };
    EXPECT_TRUE (a3app::saveAppConfig (bad, file).failed());
    EXPECT_EQ (file.loadFileAsString(), before);
    file.deleteFile();
}

TEST (ConfigEdits, SetHeadphoneEqWritesTheModel)
{
    juce::String error;
    const auto set = a3app::withHeadphoneEq (sample(), "Sennheiser-HD600", error);
    ASSERT_TRUE (set) << error;
    EXPECT_EQ (set->engine.phonesHeadphoneEq, "Sennheiser-HD600");
    EXPECT_FALSE (a3app::sameConfig (sample(), *set));
    const auto off = a3app::withHeadphoneEq (*set, "off", error);
    ASSERT_TRUE (off) << error;
    EXPECT_EQ (off->engine.phonesHeadphoneEq, "off");
    EXPECT_FALSE (a3app::withHeadphoneEq (sample(), "Acme-Buds", error));
    EXPECT_NE (error.indexOf ("Acme-Buds"), -1);
}
