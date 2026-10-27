// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <atomic>
#include <cmath>
#include <vector>
#include "AmbiEncoderBus.h"
#include "SpatialBusFixture.h"

using namespace a3test;

namespace
{
constexpr int busChannels = 16;

// Contiguous positions, one per analog source, as the engine hands them over.
struct Positions
{
    explicit Positions (size_t count) : azimuth (count), elevation (count) {}
    std::vector<std::atomic<float>> azimuth, elevation;
    const std::atomic<float>* az() const { return azimuth.data(); }
    const std::atomic<float>* el() const { return elevation.data(); }
};

// Per-source signals; an empty Stereo is passed as a null pointer (silence).
using Signals = std::vector<Stereo>;

std::vector<a3::StereoIn> at (const Signals& signals, size_t start)
{
    std::vector<a3::StereoIn> ins (signals.size());
    for (size_t c = 0; c < signals.size(); ++c)
        if (! signals[c].l.empty())
            ins[c] = { signals[c].l.data() + start, signals[c].r.data() + start };
    return ins;
}

a3::StereoIn at (const Stereo& signal, size_t start)
{
    if (signal.l.empty())
        return {};
    return { signal.l.data() + start, signal.r.data() + start };
}

Stereo impulse (size_t frames, size_t where = 0)
{
    Stereo s { std::vector<float> (frames), std::vector<float> (frames) };
    s.l[where] = s.r[where] = 1.0f;
    return s;
}

std::unique_ptr<a3::AmbiEncoderBus> makeBus (int analogChannels, int blockSize = spatialQuantum)
{
    juce::String error;
    auto bus = a3::AmbiEncoderBus::create (analogChannels, loadedSpatialConfig (shippedSpatialDirectory), error);
    if (bus == nullptr)
        throw std::runtime_error (error.toStdString());
    bus->prepare (spatialRate, blockSize);
    return bus;
}

// The whole signal through the bus with call sizes cycling through `calls`; the 16 channels it wrote.
juce::AudioBuffer<float> render (a3::AmbiEncoderBus& bus, const Signals& moving, const Signals& steady, const Stereo& ret,
                                 size_t frames, const Positions& positions, const std::vector<int>& calls = { spatialQuantum })
{
    juce::AudioBuffer<float> whole (busChannels, (int) frames);
    juce::AudioBuffer<float> call (busChannels, 4096);
    size_t k = 0;
    for (size_t start = 0; start < frames;)
    {
        const int n = (int) std::min<size_t> ((size_t) calls[k++ % calls.size()], frames - start);
        bus.process (at (moving, start), at (steady, start), at (ret, start), call, n, positions.az(), positions.el());
        for (int ch = 0; ch < busChannels; ++ch)
            whole.copyFrom (ch, (int) start, call, ch, 0, n);
        start += (size_t) n;
    }
    return whole;
}

double maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0)
{
    double worst = 0.0;
    for (int ch = 0; ch < busChannels; ++ch)
        for (int i = from; i < a.getNumSamples(); ++i)
            worst = std::max (worst, std::abs ((double) a.getSample (ch, i) - b.getSample (ch, i)));
    return worst;
}

double peak (const juce::AudioBuffer<float>& a)
{
    double loudest = 0.0;
    for (int ch = 0; ch < busChannels; ++ch)
        loudest = std::max (loudest, (double) a.getMagnitude (ch, 0, a.getNumSamples()));
    return loudest;
}

Signals silentSignals (size_t count, size_t frames) { return Signals (count, silentStereo (frames)); }

// Source `index` of `count` carries `signal` at azimuth `azimuth`, all others silent at 0.
juce::AudioBuffer<float> oneSourceAt (int count, int index, const Stereo& signal, float azimuth)
{
    const size_t frames = signal.l.size();
    auto bus = makeBus (count);
    auto moving = silentSignals ((size_t) count, frames);
    moving[(size_t) index] = signal;
    Positions positions ((size_t) count);
    positions.azimuth[(size_t) index] = azimuth;
    return render (*bus, moving, silentSignals ((size_t) count, frames), {}, frames, positions);
}
}

TEST (AmbiEncoderBus, BedFromTheShippedData)
{
    const auto bed = a3::bedFrom (loadedSpatialConfig (shippedSpatialDirectory));
    const float azimuths[] { 45.0f, -45.0f, -135.0f, 135.0f };
    const int sides[] { 0, 1, 1, 0 };   // L, R, R, L
    for (size_t k = 0; k < bed.size(); ++k)
    {
        EXPECT_FLOAT_EQ (bed[k].azimuth, azimuths[k]) << k;
        EXPECT_FLOAT_EQ (bed[k].elevation, 0.0f) << k;
        EXPECT_FLOAT_EQ (bed[k].gainDb, -6.0f) << k;
        EXPECT_EQ (bed[k].side, sides[k]) << k;
    }
}

TEST (AmbiEncoderBus, MatchesSpatialBusEncoderAtFour)
{
    const auto config = loadedSpatialConfig (shippedSpatialDirectory);
    const size_t frames = 8192;
    const auto taps = renderStrips (pinkOnAllStrips (frames));
    a3::SpatialControls controls;
    place (controls, staticPlacement);

    a3::SpatialBus reference (config);
    reference.prepare (spatialRate, spatialQuantum);
    OutputSignals out (config, frames);
    juce::AudioBuffer<float> expected (busChannels, (int) frames);
    for (size_t start = 0; start < frames; start += spatialQuantum)
    {
        reference.process (taps.at (start), out.at (start), spatialQuantum, controls);
        for (int ch = 0; ch < busChannels; ++ch)
            expected.copyFrom (ch, (int) start, reference.lastBusChunk(), ch, 0, spatialQuantum);
    }

    auto bus = makeBus (a3::numSpatialStrips);
    Positions positions (a3::numSpatialStrips);
    for (size_t i = 0; i < positions.azimuth.size(); ++i)
    {
        positions.azimuth[i] = staticPlacement[i].first;
        positions.elevation[i] = staticPlacement[i].second;
    }
    const Signals moving (taps.moving.begin(), taps.moving.end());
    const Signals steady (taps.steady.begin(), taps.steady.end());
    const auto actual = render (*bus, moving, steady, {}, frames, positions);

    EXPECT_EQ (bus->encoderCount(), 1);
    EXPECT_GT (peak (expected), 0.01);
    EXPECT_LT (maxAbsDiff (actual, expected), 1e-6);
}

TEST (AmbiEncoderBus, ReturnLandsInTheBedOnly)
{
    const size_t frames = 1024;
    Positions positions (4);
    for (size_t i = 0; i < 4; ++i)
        positions.azimuth[i] = staticPlacement[i].first;

    auto returned = makeBus (4);
    const auto viaReturn = render (*returned, silentSignals (4, frames), silentSignals (4, frames), impulse (frames, 100),
                                   frames, positions);

    auto steadied = makeBus (4);
    auto steady = silentSignals (4, frames);
    steady[2] = impulse (frames, 100);
    const auto viaSteady = render (*steadied, silentSignals (4, frames), steady, {}, frames, positions);

    EXPECT_GT (peak (viaSteady), 0.1);
    EXPECT_LT (maxAbsDiff (viaReturn, viaSteady), 1e-6);
}

TEST (AmbiEncoderBus, BuildsForOneFourEight)
{
    const size_t frames = 1024;
    const auto reference = oneSourceAt (1, 0, impulse (frames, 100), 90.0f);
    EXPECT_GT (peak (reference), 0.1);

    for (const int count : { 1, 4, 8 })
    {
        auto bus = makeBus (count);
        EXPECT_EQ (bus->encoderCount(), 1) << count;
        Positions positions ((size_t) count);
        const auto silence = render (*bus, silentSignals ((size_t) count, frames), silentSignals ((size_t) count, frames),
                                     silentStereo (frames), frames, positions);
        EXPECT_EQ (peak (silence), 0.0) << count;

        const auto last = oneSourceAt (count, count - 1, impulse (frames, 100), 90.0f);
        EXPECT_LT (maxAbsDiff (last, reference), 1e-6) << count;
    }
}

TEST (AmbiEncoderBus, NoAnalogChannelsIsBedAndReturnOnly)
{
    const size_t frames = 1024;
    auto bare = makeBus (0);
    EXPECT_EQ (bare->encoderCount(), 1);
    const auto onlyReturn = render (*bare, {}, {}, impulse (frames, 100), frames, Positions (0));

    auto four = makeBus (4);
    const auto fourReturn = render (*four, silentSignals (4, frames), silentSignals (4, frames), impulse (frames, 100),
                                    frames, Positions (4));

    EXPECT_GT (peak (onlyReturn), 0.1);
    EXPECT_LT (maxAbsDiff (onlyReturn, fourReturn), 1e-6);
}

TEST (AmbiEncoderBus, SixtyOneChannelsUseTwoEncoders)
{
    EXPECT_EQ (makeBus (60)->encoderCount(), 1);
    EXPECT_EQ (makeBus (61)->encoderCount(), 2);
    EXPECT_EQ (makeBus (124)->encoderCount(), 2);
    EXPECT_EQ (makeBus (125)->encoderCount(), 3);

    const size_t frames = 1024;
    const auto first = oneSourceAt (1, 0, impulse (frames, 100), 30.0f);
    const auto sixtyFirst = oneSourceAt (61, 60, impulse (frames, 100), 30.0f);
    EXPECT_GT (peak (first), 0.1);
    EXPECT_LT (maxAbsDiff (sixtyFirst, first), 1e-6);
}

TEST (AmbiEncoderBus, ANegativeCountIsRefused)
{
    juce::String error;
    EXPECT_EQ (a3::AmbiEncoderBus::create (-1, loadedSpatialConfig (shippedSpatialDirectory), error), nullptr);
    EXPECT_TRUE (error.isNotEmpty());
}

TEST (AmbiEncoderBus, AnyCallSize)
{
    const size_t frames = 8192;
    for (const int count : { 4, 61 })
    {
        Signals moving, steady;
        Positions positions ((size_t) count);
        for (int c = 0; c < count; ++c)
        {
            moving.push_back (pinkStereo (frames, (uint32_t) (10 + c)));
            steady.push_back (pinkStereo (frames, (uint32_t) (200 + c)));
            positions.azimuth[(size_t) c] = -180.0f + 360.0f * (float) c / (float) count;
            positions.elevation[(size_t) c] = (float) (c % 7) * 5.0f;
        }
        const auto ret = pinkStereo (frames, 999);

        auto odd = makeBus (count);
        auto even = makeBus (count);
        auto large = makeBus (count, 2048);
        const auto oddOut = render (*odd, moving, steady, ret, frames, positions, { 1, 7, 63, 64, 65, 128, 300, 13, 4096 });
        const auto evenOut = render (*even, moving, steady, ret, frames, positions);
        const auto largeOut = render (*large, moving, steady, ret, frames, positions, { 2048 });

        EXPECT_GT (peak (evenOut), 0.01) << count;
        // The first block ramps from the warm-up position to the static one over its own length.
        EXPECT_LT (maxAbsDiff (oddOut, evenOut, spatialQuantum), 1e-5) << count;
        EXPECT_LT (maxAbsDiff (largeOut, evenOut, 2048), 1e-5) << count;
    }
}
