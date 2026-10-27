// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <atomic>
#include <cmath>
#include <functional>
#include <random>
#include <thread>
#include "SpatialBusFixture.h"

using namespace a3test;

namespace
{
using a3::Tap;
constexpr int left = 0, right = 1;

std::vector<a3::EncoderInput> movingPair (int strip) { return { { strip, Tap::moving, left, 1.0f }, { strip, Tap::moving, right, 1.0f } }; }

std::vector<a3::EncoderInput> steadyOfAllStrips (int side)
{
    return { { 0, Tap::steady, side, 1.0f }, { 1, Tap::steady, side, 1.0f },
             { 2, Tap::steady, side, 1.0f }, { 3, Tap::steady, side, 1.0f } };
}

double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    double worst = 0.0;
    for (size_t i = from; i < a.size(); ++i)
        worst = std::max (worst, std::abs ((double) a[i] - b[i]));
    return worst;
}

double maxAbsDiff (const OutputSignals& a, const OutputSignals& b, size_t from)
{
    double worst = 0.0;
    for (size_t c = 0; c < a.channels.size(); ++c)
        worst = std::max (worst, maxAbsDiff (a.channels[c], b.channels[c], from));
    return worst;
}

double rmsDb (const std::vector<float>& x, size_t from)
{
    double sum = 0.0;
    for (size_t i = from; i < x.size(); ++i)
        sum += (double) x[i] * x[i];
    return 10.0 * std::log10 (sum / (double) (x.size() - from) + 1e-30);
}

bool allFinite (const OutputSignals& out)
{
    for (const auto& channel : out.channels)
        for (const float v : channel)
            if (! std::isfinite (v))
                return false;
    return true;
}

const juce::File& dataDirectory (bool reaperBooth) { return reaperBooth ? reaperBoothSpatialDirectory : shippedSpatialDirectory; }
}

// Step 0 section A: moving L + R of strip n on sources 0-3 (a plain add);
// steady L of all four strips on sources 4 and 7, steady R on 5 and 6. Every
// tap +1.0: the bed's -6 dB is the encoder's own gain4..7, the inversion is the strip's.
TEST (SpatialConfig, EncoderInputsFollowTheTemplate)
{
    const auto config = loadedSpatialConfig (shippedSpatialDirectory);
    const std::array<std::vector<a3::EncoderInput>, 8> expected {
        movingPair (0), movingPair (1), movingPair (2), movingPair (3),
        steadyOfAllStrips (left), steadyOfAllStrips (right), steadyOfAllStrips (right), steadyOfAllStrips (left)
    };
    for (size_t source = 0; source < expected.size(); ++source)
    {
        ASSERT_EQ (config.sources[source].size(), expected[source].size()) << "source " << source;
        for (size_t k = 0; k < expected[source].size(); ++k)
        {
            const auto& got = config.sources[source][k];
            const auto& want = expected[source][k];
            EXPECT_EQ (got.strip, want.strip) << "source " << source << " entry " << k;
            EXPECT_EQ (got.tap, want.tap) << "source " << source << " entry " << k;
            EXPECT_EQ (got.side, want.side) << "source " << source << " entry " << k;
            EXPECT_EQ (got.gain, want.gain) << "source " << source << " entry " << k;
        }
    }
    for (const char* id : { "gain4", "gain5", "gain6", "gain7" })
        EXPECT_EQ ((double) config.encoderParameters.getProperty (id, {}), -6.0) << id;
}

TEST (SpatialConfig, ABadEncoderInputIsRefused)
{
    using Edit = std::function<juce::String (const juce::String&)>;
    const auto firstReplaced = [] (const char* from, const char* to) -> Edit
    { return [=] (const juce::String& text) { return text.replaceFirstOccurrenceOf (from, to); }; };
    const auto sevenSources = [] (const juce::String& text)
    {
        auto parsed = juce::JSON::parse (text);
        parsed.getProperty ("sources", {}).getArray()->removeLast();
        return juce::JSON::toString (parsed);
    };
    const std::vector<std::pair<const char*, Edit>> cases {
        { "strip 5", firstReplaced ("\"strip\": 1", "\"strip\": 5") },
        { "tap middle", firstReplaced ("\"moving\"", "\"middle\"") },
        { "7 sources", sevenSources },
        { "side C", firstReplaced ("\"side\": \"L\"", "\"side\": \"C\"") },
        { "gain not a number", firstReplaced ("\"gain\": 1.0", "\"gain\": \"loud\"") },
    };
    for (const auto& [name, edit] : cases)
    {
        ScratchDirectory scratch;
        const auto file = scratch.root().getChildFile ("encoder.inputs.json");
        const auto original = file.loadFileAsString();
        const auto edited = edit (original);
        ASSERT_NE (edited, original) << name;
        ASSERT_TRUE (file.replaceWithText (edited));
        juce::String error;
        EXPECT_FALSE (a3::loadSpatialConfig (scratch.root(), error).has_value()) << name;
        EXPECT_TRUE (error.contains ("encoder.inputs.json")) << name << ": " << error;
    }
}

// The bus against an encoder node driven directly at azimuth 90 (degrees, via
// its parameter): the same eight sums in, the same 16 channels out.
TEST (SpatialBus, APositionReachesTheEncoderInDegrees)
{
    const auto config = loadedSpatialConfig (shippedSpatialDirectory);
    const auto taps = renderStrips (pinkOnOneStrip (44096, 0));

    a3::SpatialBus bus (config);
    bus.prepare (spatialRate, spatialQuantum);
    a3::SpatialControls positions;
    place (positions, { { { 90.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } } });

    auto reference = a3::IemNode::create (a3::IemKind::multiEncoder, 8, 16);
    ASSERT_TRUE (reference != nullptr);
    ASSERT_TRUE (reference->setParameters (config.encoderParameters).wasOk());
    ASSERT_TRUE (reference->setParameter ("azimuth0", 90.0f).wasOk());
    ASSERT_TRUE (reference->setParameter ("elevation0", 0.0f).wasOk());
    reference->prepare (spatialRate, spatialQuantum);

    OutputSignals out (config, spatialQuantum);
    juce::AudioBuffer<float> io (16, spatialQuantum);
    double worst = 0.0;
    double busPeak = 0.0;
    for (size_t start = 0, block = 0; start < taps.frames(); start += spatialQuantum, ++block)
    {
        bus.process (taps.at (start), out.at (0), spatialQuantum, positions);

        io.clear();
        for (int s = 0; s < 4; ++s)
            for (int side = 0; side < 2; ++side)
            {
                const auto& moving = side == 0 ? taps.moving[(size_t) s].l : taps.moving[(size_t) s].r;
                const auto& steady = side == 0 ? taps.steady[(size_t) s].l : taps.steady[(size_t) s].r;
                io.addFrom (s, 0, moving.data() + start, spatialQuantum);
                for (const int source : side == 0 ? std::array<int, 2> { 4, 7 } : std::array<int, 2> { 5, 6 })
                    io.addFrom (source, 0, steady.data() + start, spatialQuantum);
            }
        reference->process (io);

        if (block == 0)
            continue;   // the bus moves from the default position to 90 within its first block
        for (int ch = 0; ch < 16; ++ch)
            for (int i = 0; i < spatialQuantum; ++i)
            {
                worst = std::max (worst, std::abs ((double) bus.lastBusChunk().getSample (ch, i) - io.getSample (ch, i)));
                busPeak = std::max (busPeak, std::abs ((double) bus.lastBusChunk().getSample (ch, i)));
            }
    }
    EXPECT_GT (busPeak, 0.01);
    EXPECT_LT (worst, 1e-6);
}

class SpatialBusData : public testing::TestWithParam<bool> {};

INSTANTIATE_TEST_SUITE_P (DataDirectories, SpatialBusData, testing::Values (false, true),
                          [] (const testing::TestParamInfo<bool>& info) { return info.param ? "reaperBooth61" : "shipped"; });

TEST_P (SpatialBusData, AnyCallSizeMatchesTheQuantum)
{
    const auto config = loadedSpatialConfig (dataDirectory (GetParam()));
    const auto taps = renderStrips (pinkOnAllStrips (22050));
    a3::SpatialControls positions;
    place (positions, staticPlacement);

    a3::SpatialBus odd (config), even (config);
    odd.prepare (spatialRate, spatialQuantum);
    even.prepare (spatialRate, spatialQuantum);
    OutputSignals oddOut (config, taps.frames()), evenOut (config, taps.frames());
    runBus (odd, taps, oddOut, { 1, 7, 63, 64, 65, 128, 300, 13 }, positions);
    runBus (even, taps, evenOut, { spatialQuantum }, positions);

    EXPECT_EQ (oddOut.channels.size(), GetParam() ? 14u : 12u);
    EXPECT_GT (rmsDb (evenOut.channels[oddOut.mainIndices.back()], 1024), -60.0);
    EXPECT_LT (maxAbsDiff (oddOut, evenOut, 1024), 1e-5);
}

TEST (SpatialBus, CallsLongerThanThePreparedBlockAreChunked)
{
    const auto config = loadedSpatialConfig (shippedSpatialDirectory);
    const auto taps = renderStrips (pinkOnAllStrips (4096));
    a3::SpatialControls positions;
    place (positions, staticPlacement);

    a3::SpatialBus whole (config), quantum (config);
    whole.prepare (spatialRate, spatialQuantum);
    quantum.prepare (spatialRate, spatialQuantum);
    OutputSignals wholeOut (config, 4096), quantumOut (config, 4096);
    runBus (whole, taps, wholeOut, { 4096 }, positions);
    runBus (quantum, taps, quantumOut, { spatialQuantum }, positions);

    EXPECT_TRUE (allFinite (wholeOut));
    EXPECT_LT (maxAbsDiff (wholeOut, quantumOut, spatialQuantum), 1e-5);
}

TEST_P (SpatialBusData, SilenceStaysSilent)
{
    const auto config = loadedSpatialConfig (dataDirectory (GetParam()));
    a3::SpatialControls positions;
    place (positions, staticPlacement);
    a3::SpatialBus bus (config);
    bus.prepare (spatialRate, spatialQuantum);

    const size_t second = 44100;
    const auto zeros = renderStrips (silentStrips (2 * second));
    OutputSignals silent (config, zeros.frames());
    runBus (bus, zeros, silent, { spatialQuantum }, positions);
    for (size_t c = 0; c < silent.channels.size(); ++c)
        for (size_t i = 0; i < silent.channels[c].size(); ++i)
            ASSERT_EQ (silent.channels[c][i], 0.0f) << "output " << c << " frame " << i;

    auto signalThenSilence = pinkOnAllStrips (6 * second);
    for (auto& strip : signalThenSilence)
        for (size_t i = second; i < 6 * second; ++i)
            strip.l[i] = strip.r[i] = 0.0f;
    const auto taps = renderStrips (signalThenSilence);
    OutputSignals out (config, taps.frames());
    runBus (bus, taps, out, { spatialQuantum }, positions);
    EXPECT_GT (rmsDb (out.channels[out.recIndices[0]], 0), -60.0);
    float lastBlockPeak = 0.0f;
    for (const auto& channel : out.channels)
        for (size_t i = channel.size() - spatialQuantum; i < channel.size(); ++i)
            lastBlockPeak = std::max (lastBlockPeak, std::abs (channel[i]));
    EXPECT_LT (lastBlockPeak, 1e-9f);
}

// Review focus 2: positions arrive from the OSC thread as plain atomic stores.
TEST (SpatialBus, PositionsFromAnotherThreadTakeNoLock)
{
    static_assert (std::atomic<float>::is_always_lock_free);
    const auto config = loadedSpatialConfig (shippedSpatialDirectory);
    const auto taps = renderStrips (pinkOnAllStrips (44096));
    const size_t blocksInTaps = taps.frames() / spatialQuantum;
    const auto tapsOfBlock = [&] (size_t block) { return taps.at ((block % blocksInTaps) * spatialQuantum); };

    a3::SpatialBus moved (config), still (config);
    moved.prepare (spatialRate, spatialQuantum);
    still.prepare (spatialRate, spatialQuantum);
    a3::SpatialControls movedPositions, stillPositions;
    place (stillPositions, { { { 45.0f, 0.0f }, { 45.0f, 0.0f }, { 45.0f, 0.0f }, { 45.0f, 0.0f } } });

    std::thread writer ([&]
    {
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> azimuth (-180.0f, 180.0f), elevation (-90.0f, 90.0f);
        const float edges[] { -180.0f, 180.0f, -90.0f, 90.0f };
        for (int k = 0; k < 100000; ++k)
        {
            const size_t channel = (size_t) k % 4;
            const bool edge = k % 10 == 0;
            movedPositions.azimuth[channel] = edge ? edges[(k / 10) % 2] : azimuth (rng);
            movedPositions.elevation[channel] = edge ? edges[2 + (k / 10) % 2] : elevation (rng);
        }
        place (movedPositions, { { { 45.0f, 0.0f }, { 45.0f, 0.0f }, { 45.0f, 0.0f }, { 45.0f, 0.0f } } });
    });

    OutputSignals movedOut (config, spatialQuantum), stillOut (config, spatialQuantum);
    const size_t blocks = 5000;
    bool finite = true;
    for (size_t block = 0; block < blocks; ++block)
    {
        moved.process (tapsOfBlock (block), movedOut.at (0), spatialQuantum, movedPositions);
        still.process (tapsOfBlock (block), stillOut.at (0), spatialQuantum, stillPositions);
        finite = finite && allFinite (movedOut);
    }
    writer.join();
    EXPECT_TRUE (finite);

    for (size_t block = blocks; block < blocks + 2; ++block)   // one settling block, then the compared one
    {
        moved.process (tapsOfBlock (block), movedOut.at (0), spatialQuantum, movedPositions);
        still.process (tapsOfBlock (block), stillOut.at (0), spatialQuantum, stillPositions);
    }
    double worst = 0.0;
    for (int ch = 0; ch < 16; ++ch)
        for (int i = 0; i < spatialQuantum; ++i)
            worst = std::max (worst, std::abs ((double) moved.lastBusChunk().getSample (ch, i)
                                               - still.lastBusChunk().getSample (ch, i)));
    EXPECT_LT (worst, 1e-5);
    EXPECT_TRUE (allFinite (movedOut));
}

TEST (SpatialBus, TheZonesFixedGainIsApplied)
{
    auto quieter = loadedSpatialConfig (shippedSpatialDirectory);
    auto unity = quieter;
    quieter.main.fixedGainDb = -6.0f;
    unity.main.fixedGainDb = 0.0f;
    const auto taps = renderStrips (pinkOnAllStrips (22050));
    a3::SpatialControls positions;
    place (positions, staticPlacement);

    a3::SpatialBus quieterBus (quieter), unityBus (unity);
    quieterBus.prepare (spatialRate, spatialQuantum);
    unityBus.prepare (spatialRate, spatialQuantum);
    OutputSignals quieterOut (quieter, taps.frames()), unityOut (unity, taps.frames());
    runBus (quieterBus, taps, quieterOut, { spatialQuantum }, positions);
    runBus (unityBus, taps, unityOut, { spatialQuantum }, positions);

    // -6 dB is 0.501 linear: -6.00 dB, not the -6.02 dB of a factor 0.5.
    for (const auto c : quieterOut.mainIndices)
        EXPECT_NEAR (rmsDb (quieterOut.channels[c], 1024) - rmsDb (unityOut.channels[c], 1024), -6.0, 0.01) << "main output " << c;
    for (const auto& indices : { quieterOut.recIndices, quieterOut.boothIndices })
        for (const auto c : indices)
            EXPECT_EQ (maxAbsDiff (quieterOut.channels[c], unityOut.channels[c], 0), 0.0) << "output " << c;
}

// The bus's own routing, against the data: the decoders are judged in
// Layout.cpp (Zone); here the question is whether decoder channel n reaches
// the port the data names. Strip 1's moving pair carries the signal straight
// into the bus (source 0); every other tap is silent, so source 0 is the
// only thing in the field and sits exactly where azimuth0/elevation0 say.
namespace
{
constexpr size_t routingFrames = 32768;
constexpr size_t routingSettle = 12000;   // IEM's smoothing and the 150 Hz crossover have settled

TapSignals onlyStripOneMoving (const Stereo& signal)
{
    TapSignals taps;
    for (size_t s = 0; s < taps.moving.size(); ++s)
        taps.moving[s] = taps.steady[s] = silentStereo (signal.l.size());
    taps.moving[0] = signal;
    return taps;
}

OutputSignals renderAt (const a3::SpatialConfig& config, const TapSignals& taps, float azimuth, float elevation)
{
    a3::SpatialBus bus (config);
    bus.prepare (spatialRate, spatialQuantum);
    a3::SpatialControls positions;
    place (positions, { { { azimuth, elevation }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } } });
    OutputSignals out (config, taps.frames());
    runBus (bus, taps, out, { spatialQuantum }, positions);
    return out;
}

struct ZoneRoute
{
    const char* name;
    const a3::ZoneConfig& zone;
    const std::vector<size_t>& indices;   // into OutputSignals::channels: [sub,] top 1..N
};

std::vector<ZoneRoute> zoneRoutes (const a3::SpatialConfig& config, const OutputSignals& out)
{
    return { { "main", config.main, out.mainIndices }, { "booth", config.booth, out.boothIndices } };
}

size_t topIndex (const ZoneRoute& route, size_t k) { return route.indices[(route.zone.layout.hasSub() ? 1 : 0) + k]; }
}

TEST_P (SpatialBusData, ASourceAtATopsDirectionIsLoudestOnThatTopsPort)
{
    const auto config = loadedSpatialConfig (dataDirectory (GetParam()));
    const auto taps = onlyStripOneMoving (pinkStereo (routingFrames, 7));
    OutputSignals layoutOnly (config, 1);
    for (const auto& routeOfLayout : zoneRoutes (config, layoutOnly))
    {
        ASSERT_EQ (routeOfLayout.zone.topDirections.size(), routeOfLayout.zone.layout.topChannels.size());
        for (size_t k = 0; k < routeOfLayout.zone.topDirections.size(); ++k)
        {
            const auto [azimuth, elevation] = routeOfLayout.zone.topDirections[k];
            const auto out = renderAt (config, taps, azimuth, elevation);
            const auto route = zoneRoutes (config, out)[routeOfLayout.name == std::string ("main") ? 0 : 1];
            const double aimed = rmsDb (out.channels[topIndex (route, k)], routingSettle);
            for (size_t other = 0; other < route.zone.topDirections.size(); ++other)
                if (other != k)
                    EXPECT_GE (aimed - rmsDb (out.channels[topIndex (route, other)], routingSettle), 3.0)
                        << route.name << ": source at top " << k + 1 << ", louder top " << other + 1;
        }
    }
}

TEST_P (SpatialBusData, ALowToneReachesEachZonesSubPort)
{
    const auto config = loadedSpatialConfig (dataDirectory (GetParam()));
    const auto out = renderAt (config, onlyStripOneMoving (sine (routingFrames, 50.0, spatialRate, 0.25f)), 0.0f, 0.0f);
    for (const auto& route : zoneRoutes (config, out))
    {
        ASSERT_TRUE (route.zone.layout.hasSub()) << route.name;
        const double sub = rmsDb (out.channels[route.indices[0]], routingSettle);
        EXPECT_GT (sub, -40.0) << route.name;
        for (size_t k = 0; k < route.zone.layout.topChannels.size(); ++k)
            EXPECT_GE (sub - rmsDb (out.channels[topIndex (route, k)], routingSettle), 10.0)
                << route.name << ": top " << k + 1;
    }
}

TEST_P (SpatialBusData, ASourceOnTheLeftIsLouderOnRecL)
{
    const auto config = loadedSpatialConfig (dataDirectory (GetParam()));
    const auto out = renderAt (config, onlyStripOneMoving (pinkStereo (routingFrames, 7)), 90.0f, 0.0f);
    const double recL = rmsDb (out.channels[out.recIndices[0]], routingSettle);
    const double recR = rmsDb (out.channels[out.recIndices[1]], routingSettle);
    EXPECT_GE (recL - recR, 3.0) << "rec_L " << recL << " dB, rec_R " << recR << " dB";
}
