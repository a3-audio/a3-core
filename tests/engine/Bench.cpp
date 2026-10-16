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
