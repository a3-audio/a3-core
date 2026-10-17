// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>
#include "BlockTimer.h"
#include "ChannelStrip.h"
#include "Generator.h"
#include "OscControl.h"
#include "SpatialBus.h"
#include "SpatialBusFixture.h"
#include "SpatialPorts.h"

namespace
{
struct Rendered { std::vector<float> l, r; };

Rendered render (a3bench::Generator& g, int frames, int block = 480)
{
    Rendered out { std::vector<float> ((size_t) frames), std::vector<float> ((size_t) frames) };
    for (int at = 0; at < frames; at += block)
    {
        const int n = std::min (block, frames - at);
        g.render (out.l.data() + at, out.r.data() + at, n);
    }
    return out;
}

double rmsDb (const std::vector<float>& v)
{
    double sum = 0.0;
    for (float x : v)
        sum += (double) x * x;
    return 10.0 * std::log10 (sum / (double) v.size());
}

std::string truthText()
{
    std::ifstream file (A3_TRUTH_COPY);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

struct Strips
{
    a3::ChannelStrip s[4];
    std::array<a3::StripControls*, 4> pointers() { return { &s[0].controls(), &s[1].controls(),
                                                            &s[2].controls(), &s[3].controls() }; }
};
}

TEST (BenchGenerator, ASineAtMinusTwentyDbfsPeaksAtOneTenth)
{
    a3bench::Generator g;
    g.prepare (48000.0);
    g.set (a3bench::Shape::sine, 1000.0, -20.0f);
    const auto out = render (g, 48000);
    float peak = 0.0f;
    for (float x : out.l)
        peak = std::max (peak, std::abs (x));
    EXPECT_NEAR (peak, 0.1f, 0.0001f);
    EXPECT_EQ (out.l, out.r);
}

TEST (BenchGenerator, OffRendersZeros)
{
    a3bench::Generator g;
    g.prepare (48000.0);
    g.set (a3bench::Shape::sine, 1000.0, -20.0f);
    render (g, 480);
    g.set (a3bench::Shape::off, 0.0, 0.0f);
    const auto out = render (g, 4800);
    for (size_t i = 0; i < out.l.size(); ++i)
        ASSERT_EQ (out.l[i] + out.r[i], 0.0f) << i;
}

TEST (BenchGenerator, PinkNoiseRmsIsTheLevelWithinOneDb)
{
    a3bench::Generator g;
    g.prepare (48000.0);
    g.set (a3bench::Shape::pink, 0.0, -20.0f);
    EXPECT_NEAR (rmsDb (render (g, 48000).l), -20.0, 1.0);
}

TEST (BenchGenerator, TheSweepStaysAtItsLevelAndNeverExceedsIt)
{
    a3bench::Generator g;
    g.prepare (48000.0);
    g.set (a3bench::Shape::sweep, 0.0, -20.0f);
    const auto out = render (g, 48000 * 3);
    float peak = 0.0f;
    for (float x : out.l)
        peak = std::max (peak, std::abs (x));
    EXPECT_NEAR (peak, 0.1f, 0.001f);
}

TEST (BenchGenSpec, TheThreeShapesParse)
{
    a3bench::GenSpec g;
    ASSERT_TRUE (a3bench::parseGenSpec ("sine:1000:-20", g));
    EXPECT_EQ (g.shape, a3bench::Shape::sine);
    EXPECT_EQ (g.hz, 1000.0);
    EXPECT_EQ (g.levelDb, -20.0f);
    ASSERT_TRUE (a3bench::parseGenSpec ("pink:-18.5", g));
    EXPECT_EQ (g.shape, a3bench::Shape::pink);
    EXPECT_EQ (g.levelDb, -18.5f);
    ASSERT_TRUE (a3bench::parseGenSpec ("sweep:-20", g));
    EXPECT_EQ (g.shape, a3bench::Shape::sweep);
}

TEST (BenchGenSpec, NonsenseIsRefusedAndLeavesTheSpecAlone)
{
    a3bench::GenSpec g;
    g.hz = 7.0;
    for (const char* bad : { "", "sine", "sine:1000", "sine:x:-20", "sine:0:-20", "sine:1000:-20:1",
                             "pink", "pink:nan", "pink:+6", "square:-20", "sweep:-20:1" })
        EXPECT_FALSE (a3bench::parseGenSpec (bad, g)) << bad;
    EXPECT_EQ (g.hz, 7.0);
}

TEST (BenchOsc, TheTruthNamesTheEnginePortAndTheMeterNumbers)
{
    const auto truth = a3bench::parseTruth (truthText());
    ASSERT_TRUE (truth.has_value());
    EXPECT_EQ (truth->enginePort, 9004);
    EXPECT_EQ (truth->engineBindAddress, "127.0.0.1");
    EXPECT_EQ (truth->vuAddressFor ("in1_pre_L"), "/vu/51");
    EXPECT_EQ (truth->vuAddressFor ("in4_post_R"), "/vu/66");
    EXPECT_EQ (truth->vuAddressFor ("no_such_meter"), "");
}

TEST (BenchOsc, ATruthWithoutTheEngineIsRefused)
{
    EXPECT_FALSE (a3bench::parseTruth ("{\"listeners\": [], \"addresses\": {}}").has_value());
    EXPECT_FALSE (a3bench::parseTruth ("not json").has_value());
}

TEST (BenchOsc, AChannelMessageSetsThatStripsControl)
{
    const auto truth = a3bench::parseTruth (truthText());
    ASSERT_TRUE (truth.has_value());
    Strips strips;
    auto p = strips.pointers();
    EXPECT_TRUE (a3bench::applyChannelMessage (truth->controls, "/channel/3/volume", 0.7f, p));
    EXPECT_FLOAT_EQ (strips.s[2].controls().volume.load(), 0.7f);
    EXPECT_TRUE (a3bench::applyChannelMessage (truth->controls, "/channel/1/gain", 0.25f, p));
    EXPECT_FLOAT_EQ (strips.s[0].controls().gain.load(), 0.25f);
    EXPECT_TRUE (a3bench::applyChannelMessage (truth->controls, "/channel/4/3d", 1.0f, p));
    EXPECT_FLOAT_EQ (strips.s[3].controls().threeD.load(), 1.0f);
    EXPECT_TRUE (a3bench::applyChannelMessage (truth->controls, "/channel/2/eq/high", 0.1f, p));
    EXPECT_TRUE (a3bench::applyChannelMessage (truth->controls, "/channel/2/eq/mid", 0.2f, p));
    EXPECT_TRUE (a3bench::applyChannelMessage (truth->controls, "/channel/2/eq/low", 0.3f, p));
    EXPECT_TRUE (a3bench::applyChannelMessage (truth->controls, "/channel/2/filter/frequency", 0.4f, p));
    EXPECT_TRUE (a3bench::applyChannelMessage (truth->controls, "/channel/2/filter/q", 0.6f, p));
    auto& c = strips.s[1].controls();
    EXPECT_FLOAT_EQ (c.eqHigh.load(), 0.1f);
    EXPECT_FLOAT_EQ (c.eqMid.load(), 0.2f);
    EXPECT_FLOAT_EQ (c.eqLow.load(), 0.3f);
    EXPECT_FLOAT_EQ (c.frequency.load(), 0.4f);
    EXPECT_FLOAT_EQ (c.q.load(), 0.6f);
}

TEST (BenchOsc, WhatItDoesNotOwnIsRefusedAndChangesNothing)
{
    const auto truth = a3bench::parseTruth (truthText());
    ASSERT_TRUE (truth.has_value());
    Strips strips;
    auto p = strips.pointers();
    const float before = strips.s[0].controls().gain.load();
    EXPECT_FALSE (a3bench::applyChannelMessage (truth->controls, "/channel/5/gain", 0.9f, p));
    EXPECT_FALSE (a3bench::applyChannelMessage (truth->controls, "/channel/0/gain", 0.9f, p));
    EXPECT_FALSE (a3bench::applyChannelMessage (truth->controls, "/channel/x/gain", 0.9f, p));
    EXPECT_FALSE (a3bench::applyChannelMessage (truth->controls, "/channel/1/filter", 0.9f, p));
    EXPECT_FALSE (a3bench::applyChannelMessage (truth->controls, "/channel/1/gain/extra", 0.9f, p));
    EXPECT_FALSE (a3bench::applyChannelMessage (truth->controls, "/master/volume", 0.9f, p));
    EXPECT_FALSE (a3bench::applyChannelMessage (truth->controls, "", 0.9f, p));
    EXPECT_EQ (strips.s[0].controls().gain.load(), before);
}

TEST (BenchOsc, NonFiniteValuesAreRefusedAndChangeNothing)
{
    const auto truth = a3bench::parseTruth (truthText());
    ASSERT_TRUE (truth.has_value());
    Strips strips;
    auto p = strips.pointers();
    for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity() })
    {
        EXPECT_FALSE (a3bench::applyChannelMessage (truth->controls, "/channel/1/gain", bad, p));
        EXPECT_EQ (strips.s[0].controls().gain.load(), 0.0f);
    }
}

TEST (BenchOsc, AnOutOfRangeValueIsClampedToTheWiresRange)
{
    const auto truth = a3bench::parseTruth (truthText());
    ASSERT_TRUE (truth.has_value());
    Strips strips;
    auto p = strips.pointers();
    EXPECT_TRUE (a3bench::applyChannelMessage (truth->controls, "/channel/1/volume", 3.0f, p));
    EXPECT_EQ (strips.s[0].controls().volume.load(), 1.0f);
    EXPECT_TRUE (a3bench::applyChannelMessage (truth->controls, "/channel/1/volume", -2.0f, p));
    EXPECT_EQ (strips.s[0].controls().volume.load(), 0.0f);
}

TEST (BenchBlockTimer, MeanP99AndWorstOfOneToAHundred)
{
    a3bench::BlockTimer t;
    for (int i = 1; i <= 100; ++i)
        t.add ((double) i);
    EXPECT_DOUBLE_EQ (t.mean(), 50.5);
    EXPECT_DOUBLE_EQ (t.p99(), 99.0);
    EXPECT_DOUBLE_EQ (t.worst(), 100.0);
}

TEST (BenchBlockTimer, AnEmptyTimerReadsZeroAndClearEmptiesIt)
{
    a3bench::BlockTimer t;
    EXPECT_EQ (t.mean() + t.p99() + t.worst(), 0.0);
    t.add (5.0);
    t.clear();
    EXPECT_EQ (t.worst(), 0.0);
}

TEST (BenchMetersTarget, NeedsAnExplicitHostAndPort)
{
    a3bench::MetersTarget t;
    for (const char* bad : { "--meters-osc", "--meters-osc=", "--meters-osc=host", "--meters-osc=host:x",
                             "--meters-osc=host:70000", "--meters-osc=host:0", "--meters-osc=:9100",
                             "--meters-osc=host:-1", "--meters-osc=host:91 00", "--timing" })
        EXPECT_FALSE (a3bench::parseMetersTarget (bad, t)) << bad;
    ASSERT_TRUE (a3bench::parseMetersTarget ("--meters-osc=127.0.0.1:9100", t));
    EXPECT_EQ (t.host, "127.0.0.1");
    EXPECT_EQ (t.port, 9100);
    ASSERT_TRUE (a3bench::parseMetersTarget ("--meters-osc=localhost:65535", t));
    EXPECT_EQ (t.port, 65535);
}

TEST (BenchMetersTarget, AListenersPortIsFlaggedByName)
{
    const auto truth = a3bench::parseTruth (truthText());
    ASSERT_TRUE (truth.has_value());
    EXPECT_TRUE (truth->listenerOnPort (7772).contains ("motion.vu"));
    EXPECT_TRUE (truth->listenerOnPort (9100).isEmpty());
}

TEST (BenchOsc, TheTruthNamesThePositionKeys)
{
    const auto truth = a3bench::parseTruth (truthText());
    ASSERT_TRUE (truth.has_value());
    ASSERT_EQ (truth->positions.entries.size(), 2u);
    for (const auto& e : truth->positions.entries)
    {
        EXPECT_EQ (e.firstChannel, 1);
        EXPECT_EQ (e.lastChannel, 4);
    }
    EXPECT_NE (truth->positions.entries[0].elevation, truth->positions.entries[1].elevation);
}

TEST (BenchOsc, ATruthWithoutPositionsIsRefused)
{
    for (const char* key : { "channel.azimuth", "channel.elevation" })
    {
        auto text = juce::String (truthText());
        ASSERT_TRUE (text.contains (juce::String ("\"") + key + "\""));
        EXPECT_FALSE (a3bench::parseTruth (text.replace (juce::String ("\"") + key + "\"", "\"channel.other\"")).has_value()) << key;
    }
}

namespace
{
struct Positions
{
    a3bench::Truth truth;
    a3::SpatialControls controls;
    Positions() : truth (*a3bench::parseTruth (truthText())) {}
    bool apply (const char* address, float value) { return a3bench::applyPositionMessage (truth.positions, address, value, controls); }
};
}

TEST (BenchOsc, PositionsAreClampedLikeCore)
{
    Positions p;
    EXPECT_TRUE (p.apply ("/channel/2/azimuth", 37.5f));
    EXPECT_EQ (p.controls.azimuth[1].load(), 37.5f);
    EXPECT_TRUE (p.apply ("/channel/2/azimuth", 200.0f));
    EXPECT_EQ (p.controls.azimuth[1].load(), 180.0f);
    EXPECT_TRUE (p.apply ("/channel/2/azimuth", -200.0f));
    EXPECT_EQ (p.controls.azimuth[1].load(), -180.0f);
    EXPECT_TRUE (p.apply ("/channel/4/elevation", 100.0f));
    EXPECT_EQ (p.controls.elevation[3].load(), 90.0f);
    EXPECT_TRUE (p.apply ("/channel/4/elevation", -95.0f));
    EXPECT_EQ (p.controls.elevation[3].load(), -90.0f);
}

TEST (BenchOsc, NonFinitePositionsAreRefused)
{
    Positions p;
    p.apply ("/channel/1/azimuth", 12.0f);
    for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity() })
    {
        EXPECT_FALSE (p.apply ("/channel/1/azimuth", bad));
        EXPECT_FALSE (p.apply ("/channel/1/elevation", bad));
    }
    EXPECT_EQ (p.controls.azimuth[0].load(), 12.0f);
    EXPECT_EQ (p.controls.elevation[0].load(), 0.0f);
}

TEST (BenchOsc, PositionsOutsideTheChannelsAreRefused)
{
    Positions p;
    EXPECT_FALSE (p.apply ("/channel/5/azimuth", 10.0f));
    EXPECT_FALSE (p.apply ("/channel/0/azimuth", 10.0f));
    EXPECT_FALSE (p.apply ("/channel/1/azimuthx", 10.0f));
    EXPECT_FALSE (p.apply ("/channel//azimuth", 10.0f));
    EXPECT_FALSE (p.apply ("/master/azimuth", 10.0f));
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ (p.controls.azimuth[i].load(), 0.0f);
}

TEST (BenchOsc, StripKeysAndPositionKeysDoNotCatchEachOther)
{
    Positions p;
    Strips strips;
    auto pointers = strips.pointers();
    EXPECT_FALSE (p.apply ("/channel/1/volume", 0.5f));
    EXPECT_FALSE (a3bench::applyChannelMessage (p.truth.controls, "/channel/1/azimuth", 0.5f, pointers));
    EXPECT_FALSE (a3bench::applyChannelMessage (p.truth.controls, "/channel/1/elevation", 0.5f, pointers));
}

TEST (BenchPorts, TheTwelveNamesInOrder)
{
    const auto config = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    const std::vector<std::string> expected { "rec_L", "rec_R", "main_sub", "main_top_1", "main_top_2", "main_top_3",
                                              "main_top_4", "booth_sub", "booth_top_1", "booth_top_2", "booth_top_3",
                                              "booth_top_4" };
    EXPECT_EQ (a3bench::spatialPortNames (config), expected);
}

TEST (BenchPorts, TheReaperBoothGivesFourteen)
{
    const auto config = a3test::loadedSpatialConfig (a3test::reaperBoothSpatialDirectory);
    const std::vector<std::string> expected { "rec_L", "rec_R", "main_sub", "main_top_1", "main_top_2", "main_top_3",
                                              "main_top_4", "booth_sub", "booth_top_1", "booth_top_2", "booth_top_3",
                                              "booth_top_4", "booth_top_5", "booth_top_6" };
    EXPECT_EQ (a3bench::spatialPortNames (config), expected);
}

TEST (BenchPorts, CountsFollowTheLayout)
{
    auto config = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    config.booth.layout.topChannels.resize (2);
    const auto names = a3bench::spatialPortNames (config);
    EXPECT_EQ (names.size(), 10u);
    EXPECT_EQ (names[names.size() - 2], "booth_top_1");
    EXPECT_EQ (names.back(), "booth_top_2");
}

TEST (BenchPorts, AZoneWithoutSubHasNoSubPort)
{
    auto config = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    config.main.layout.subChannel = 0;
    const auto names = a3bench::spatialPortNames (config);
    EXPECT_EQ (names.size(), 11u);
    EXPECT_EQ (std::count (names.begin(), names.end(), "main_sub"), 0);
    EXPECT_EQ (std::count (names.begin(), names.end(), "booth_sub"), 1);
}

TEST (BenchSpatialDirectory, TheArgumentWinsThenTheUserConfigThenThePackage)
{
    const auto everythingExists = [] (const std::string&) { return true; };
    const auto nothingExists = [] (const std::string&) { return false; };
    EXPECT_EQ (a3bench::chooseSpatialDirectory ("/x/data", "/home/u", everythingExists), "/x/data");
    EXPECT_EQ (a3bench::chooseSpatialDirectory ("", "/home/u", everythingExists), "/home/u/.config/a3/engine/spatial");
    EXPECT_EQ (a3bench::chooseSpatialDirectory ("", "/home/u", nothingExists), "/usr/share/a3-engine/spatial");
    EXPECT_EQ (a3bench::chooseSpatialDirectory ("", "", everythingExists), "/usr/share/a3-engine/spatial");
}

TEST (BenchSpatialBus, ABadConfigEndsInAnErrorNotAnException)
{
    auto config = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    juce::String error;
    EXPECT_NE (a3::SpatialBus::create (config, error), nullptr);
    EXPECT_TRUE (error.isEmpty());

    config.encoderParameters = juce::var (new juce::DynamicObject());
    config.encoderParameters.getDynamicObject()->setProperty ("noSuchParameter", 1.0);
    error = {};
    std::unique_ptr<a3::SpatialBus> bus;
    EXPECT_NO_THROW (bus = a3::SpatialBus::create (config, error));
    EXPECT_EQ (bus, nullptr);
    EXPECT_TRUE (error.isNotEmpty());
}
