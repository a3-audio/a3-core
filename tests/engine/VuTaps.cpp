// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's /vu meters (O13). The first five are beat-analyzer's tests/vu_meter_test.cpp and the
// VuTaps.* port-map cases its tests/vu_ports_test.cpp (7dd100b), ported by name: the analyzer's input
// order is the truth's vu_meters order here, its port names the truth's meter names.
#include <cmath>
#include <set>
#include <gtest/gtest.h>
#include "EngineFixture.h"
#include "HeapCounter.h"
#include "VuTaps.h"
#include "VuTestData.h"

namespace
{
// vu_meter_test.cpp's block: 256 frames, one sample at `level`, the next at -level/2.
std::vector<float> block (float level, int frames = 256)
{
    std::vector<float> samples ((size_t) frames, 0.0f);
    samples[(size_t) frames / 2] = level;
    samples[(size_t) frames / 2 + 1] = -level * 0.5f;
    return samples;
}

void feed (a3::VuTap& tap, const std::vector<float>& samples) { tap.processMono (samples.data(), (int) samples.size()); }

bool near (float a, float b) { return std::fabs (a - b) < 1e-6f; }

a3::VuTaps truthTaps() { return a3::VuTaps (a3test::truthVuMeters(), a3::VuSettings {}); }

bool startsWith (const std::string& text, const std::string& prefix) { return text.rfind (prefix, 0) == 0; }
}

TEST (VuTaps, ALoudBlockShowsOnceAtFullLevel)
{
    a3::VuTap tap { a3::VuSettings {} };
    feed (tap, block (1.0f));
    EXPECT_TRUE (near (tap.takePeak(), 1.0f));
}

TEST (VuTaps, TheNextTickShowsTheNewBlockNotAFall)
{
    a3::VuTap tap { a3::VuSettings {} };
    feed (tap, block (1.0f));
    tap.takePeak();
    feed (tap, block (0.1f));
    EXPECT_TRUE (near (tap.takePeak(), 0.1f));
}

TEST (VuTaps, SilenceAfterAPeakReadsZero)
{
    a3::VuTap tap { a3::VuSettings {} };
    feed (tap, block (1.0f));
    tap.takePeak();
    feed (tap, block (0.0f));
    EXPECT_TRUE (near (tap.takePeak(), 0.0f));
}

TEST (VuTaps, TheHighestBlockBetweenTwoSendsWins)
{
    a3::VuTap tap { a3::VuSettings {} };
    feed (tap, block (0.2f));
    feed (tap, block (0.9f));
    feed (tap, block (0.3f));
    EXPECT_TRUE (near (tap.takePeak(), 0.9f));
}

TEST (VuTaps, ASendWithoutAudioReadsZero)
{
    a3::VuTap tap { a3::VuSettings {} };
    feed (tap, block (0.7f));
    tap.takePeak();
    EXPECT_TRUE (near (tap.takePeak(), 0.0f));
}

// The analyzer's followRms with the rig's 0.8 / 0.2: per block, towards a louder one with attack,
// towards a quieter one with release.
TEST (VuTaps, RmsFollowsTheBlocksWithTheRigsAttackAndRelease)
{
    a3::VuTap tap { a3::VuSettings {} };
    const std::vector<float> loud (128, 0.5f), quiet (128, 0.0f);
    feed (tap, loud);
    EXPECT_FLOAT_EQ (tap.rms(), 0.8f * 0.5f);
    feed (tap, loud);
    EXPECT_FLOAT_EQ (tap.rms(), 0.8f * 0.5f + 0.2f * 0.4f);
    const float before = tap.rms();
    feed (tap, quiet);
    EXPECT_FLOAT_EQ (tap.rms(), 0.8f * before);
}

TEST (VuTaps, TheMapNamesFortyInputs)
{
    const auto taps = truthTaps();
    ASSERT_EQ (a3::VuTaps::mapChannels, 40);
    std::set<std::string> names (taps.names().begin(), taps.names().begin() + a3::VuTaps::mapChannels);
    EXPECT_EQ (names.size(), 40u);
}

TEST (VuTaps, EachBlockByItsReaperOutput)
{
    const auto taps = truthTaps();
    const auto& names = taps.names();
    ASSERT_EQ (names.size(), 56u);
    EXPECT_EQ (names[0], "analog1_L");
    EXPECT_EQ (names[1], "analog1_R");
    EXPECT_EQ (names[2], "analog2_L");
    EXPECT_EQ (names[5], "analog3_R");
    EXPECT_EQ (names[6], "analog4_L");
    EXPECT_EQ (names[7], "analog4_R");
    EXPECT_EQ (names[8], "free39");
    EXPECT_EQ (names[9], "free40");
    EXPECT_EQ (names[10], "main_sub");
    EXPECT_EQ (names[11], "main_top1");
    EXPECT_EQ (names[19], "main_top9");
    EXPECT_EQ (names[20], "booth_sub");
    EXPECT_EQ (names[29], "booth_top9");
    EXPECT_EQ (names[30], "phones_L");
    EXPECT_EQ (names[31], "phones_R");
    EXPECT_EQ (names[32], "rec_L");
    EXPECT_EQ (names[35], "aux_R");
    EXPECT_EQ (names[36], "deck_a");
    EXPECT_EQ (names[39], "deck_d");
}

TEST (VuTaps, TheStereoChannelMetersFollowTheForty)
{
    const auto taps = truthTaps();
    const auto& names = taps.names();
    ASSERT_EQ (a3::VuTaps::slots, 56);
    ASSERT_EQ (names.size(), 56u);
    EXPECT_EQ (names[40], "in1_pre_L");
    EXPECT_EQ (names[41], "in1_pre_R");
    EXPECT_EQ (names[46], "in4_pre_L");
    EXPECT_EQ (names[47], "in4_pre_R");
    EXPECT_EQ (names[48], "in1_post_L");
    EXPECT_EQ (names[55], "in4_post_R");
    EXPECT_EQ (std::set<std::string> (names.begin(), names.end()).size(), 56u);
}

// The analyzer clamped NUM_VU_CHANNELS to what its arrays held; the engine always has the 56 the rig
// runs, whatever the truth's list holds beyond them (StemDeck's 41-50 are never a slot).
TEST (VuTaps, TheCountIsHeldToWhatFits)
{
    auto longer = a3test::truthVuMeters();
    for (int extra = 0; extra < 10; ++extra)
        longer.push_back ("beyond" + std::to_string (extra));
    a3::VuTaps taps (longer, a3::VuSettings {});
    std::vector<float> peaks, rms;
    taps.take (peaks, rms);
    EXPECT_EQ (peaks.size(), 56u);
    EXPECT_EQ (rms.size(), 56u);
    EXPECT_EQ (taps.tapFor ("beyond3"), nullptr);
    for (const auto& name : longer)
        if (startsWith (name, "stem_"))
            EXPECT_EQ (taps.tapFor (name), nullptr) << name;

    a3::VuTaps shorter ({ "analog1_L", "analog1_R" }, a3::VuSettings {});
    shorter.take (peaks, rms);
    EXPECT_EQ (peaks.size(), 56u);
    EXPECT_NE (shorter.tapFor ("analog1_R"), nullptr);
    EXPECT_EQ (shorter.tapFor ("main_sub"), nullptr);
}

// Every analyzer port the patchbay fed (and every one it left unwired) has its place in
// vu-points.json; each listed name is the truth's meter of that number, and has a tap unless it is free.
// The engine places every tap it has a point for: what stays unplaced are only the zones' tops beyond
// the shipped 4.1 layout (they stay 0, as the analyzer's unwired ports did).
TEST (VuTaps, EveryAnalyzerPortHasItsPoint)
{
    const auto truth = a3test::truthVuMeters();
    auto taps = truthTaps();
    const auto points = a3test::vuPoints();
    ASSERT_EQ (points.size(), 56);

    std::set<std::string> listedPorts;
    for (int i = 0; i < points.size(); ++i)
    {
        const auto& p = points[i];
        const int n = p["n"];
        const auto name = p["name"].toString().toStdString();
        const auto kind = p["kind"].toString();
        ASSERT_GE (n, 1);
        ASSERT_LE (n, (int) truth.size());
        EXPECT_EQ (truth[(size_t) n - 1], name) << n;
        EXPECT_EQ (a3::VuTaps::vuNumber (i), n);
        EXPECT_EQ (p["analyzerPort"].toString().toStdString(), "vu_" + name);
        listedPorts.insert ("vu_" + name);
        if (kind == "free")
            EXPECT_EQ (taps.tapFor (name), nullptr) << name;
        else
            EXPECT_NE (taps.tapFor (name), nullptr) << name;
    }

    const auto patchbay = juce::XmlDocument::parse (juce::File (A3_PATCHBAY_COPY));
    ASSERT_NE (patchbay, nullptr);
    int wired = 0;
    for (const auto* socket : patchbay->getChildByName ("input-sockets")->getChildIterator())
        for (const auto* plug : socket->getChildIterator())
        {
            const auto port = plug->getAllSubText().toStdString();
            if (! startsWith (port, "vu_"))
                continue;
            ++wired;
            EXPECT_EQ (listedPorts.count (port), 1u) << port << " is not in vu-points.json";
        }
    EXPECT_EQ (wired, 50);

    a3test::EngineRig rig (a3test::engineConfig (4, 2), 64);
    const auto unplaced = rig.engine->attachVuTaps (taps);
    const auto& main = rig.spatial.main.layout;
    const auto& booth = rig.spatial.booth.layout;
    std::set<std::string> beyond;
    for (size_t top = main.topChannels.size() + 1; top <= 9; ++top)
        beyond.insert ("main_top" + std::to_string (top));
    for (size_t top = booth.topChannels.size() + 1; top <= 9; ++top)
        beyond.insert ("booth_top" + std::to_string (top));
    ASSERT_FALSE (beyond.empty());
    for (const char* deck : { "deck_c", "deck_d" })   // two deck slots configured: C and D stay 0
        beyond.insert (deck);
    EXPECT_EQ (std::set<std::string> (unplaced.begin(), unplaced.end()), beyond);
}

TEST (VuTaps, FreeNumbersAreZero)
{
    auto taps = truthTaps();
    const std::vector<float> loud (128, 0.9f);
    for (const auto& name : taps.names())
        if (auto* tap = taps.tapFor (name))
            tap->processMono (loud.data(), (int) loud.size());
    for (const char* free : { "free39", "free40" })
        EXPECT_EQ (taps.tapFor (free), nullptr) << free;

    std::vector<float> peaks, rms;
    taps.take (peaks, rms);
    ASSERT_EQ (peaks.size(), 56u);
    for (int slot = 0; slot < 56; ++slot)
    {
        const int n = a3::VuTaps::vuNumber (slot);
        const bool free = n >= 9 && n <= 10;
        if (free)
        {
            EXPECT_EQ (peaks[(size_t) slot], 0.0f) << n;
            EXPECT_EQ (rms[(size_t) slot], 0.0f) << n;
        }
        else
        {
            EXPECT_FLOAT_EQ (peaks[(size_t) slot], 0.9f) << n;
            EXPECT_GT (rms[(size_t) slot], 0.0f) << n;
        }
    }
    taps.take (peaks, rms);
    EXPECT_EQ (peaks[0], 0.0f);   // taken
}

// Real time: a tap's processMono allocates nothing, and an Engine with every tap attached allocates at
// the quantum exactly what it does without them (IEM's SimpleDecoder, one per zone and call).
TEST (VuTaps, ProcessAllocatesNothing)
{
    auto taps = truthTaps();
    const std::vector<float> signal (128, 0.25f);
    const auto direct = a3test::heapDuring ([&]
    {
        for (const auto& name : taps.names())
            if (auto* tap = taps.tapFor (name))
                tap->processMono (signal.data(), (int) signal.size());
    });
    EXPECT_EQ (direct.allocated, 0);
    EXPECT_EQ (direct.released, 0);

    constexpr int quantum = 128, calls = 64;
    a3test::EngineRig rig (a3test::engineConfig (4, 2), (size_t) (quantum * calls), quantum);
    for (int pair = 1; pair <= 6; ++pair)
        rig.setAnalogPair (pair, a3test::Stereo { std::vector<float> (rig.frames, 0.1f), std::vector<float> (rig.frames, -0.1f) });
    rig.engine->attachVuTaps (taps);
    std::vector<float> peaks, rms;
    taps.take (peaks, rms);   // the direct feed's peaks
    a3test::HeapCount total;
    for (int c = 0; c < calls; ++c)
    {
        const auto in = rig.inputsAt ((size_t) (c * quantum));
        const auto out = rig.outputsAt ((size_t) (c * quantum));
        const auto count = a3test::heapDuring ([&] { rig.engine->process (in, out, quantum, rig.controls); });
        total.allocated += count.allocated;
        total.released += count.released;
    }
    EXPECT_EQ (total.allocated, 2 * calls);
    EXPECT_EQ (total.released, 2 * calls);
    taps.take (peaks, rms);
    EXPECT_FLOAT_EQ (peaks[0], 0.1f);   // the engine fed the taps
}
