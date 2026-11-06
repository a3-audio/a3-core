// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The headphone decoders (rec, phones) and the delay run at 2nd order on the engine's 3rd-order bus:
// they read ACN 0..8 only, and the delay leaves ACN 9..15 silent. The delay at 2nd order is the
// 3rd-order delay fed only ACN 0..8; the headphone decoder is IEM's own 2nd-order BinauralDecoder
// (its 2nd-order HRIR set, not the 3rd-order set cut to nine channels), so it is pinned to its
// input instead: whatever ACN 9..15 carry changes nothing.
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <random>
#include "DelayModulation.h"
#include "EngineStages.h"
#include "IemNode.h"
#include "SpatialConfig.h"
#include "SpatialTestData.h"

namespace
{
using Channels = std::vector<std::vector<float>>;
constexpr int busChannels = 16;
constexpr int secondOrderChannels = 9;
constexpr double rate = 44100.0;
constexpr int block = 64;

a3::SpatialConfig shippedConfig()
{
    juce::String error;
    auto config = a3::loadSpatialConfig (a3test::shippedSpatialDirectory, error);
    EXPECT_TRUE (config.has_value()) << error;
    return std::move (*config);
}

Channels noise (size_t frames, unsigned seed)
{
    std::mt19937 generator (seed);
    std::uniform_real_distribution<float> uniform (-0.25f, 0.25f);
    Channels channels ((size_t) busChannels, std::vector<float> (frames));
    for (auto& channel : channels)
        for (auto& sample : channel)
            sample = uniform (generator);
    return channels;
}

Channels withoutThirdOrder (Channels channels)
{
    for (size_t ch = secondOrderChannels; ch < channels.size(); ++ch)
        std::fill (channels[ch].begin(), channels[ch].end(), 0.0f);
    return channels;
}

// Both sides of a HeadphoneDecoder fed `in` block by block.
Channels decode (a3::HeadphoneDecoder& decoder, const Channels& in)
{
    const size_t frames = in[0].size();
    Channels out (2, std::vector<float> (frames));
    juce::AudioBuffer<float> bus (busChannels, block);
    for (size_t start = 0; start + block <= frames; start += block)
    {
        for (int ch = 0; ch < busChannels; ++ch)
            bus.copyFrom (ch, 0, in[(size_t) ch].data() + start, block);
        decoder.process (bus, block);
        for (int side = 0; side < 2; ++side)
            std::copy_n (decoder.side (side), block, out[(size_t) side].begin() + (long) start);
    }
    return out;
}

double largestDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    double worst = 0.0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        worst = std::max (worst, (double) std::abs (a[i] - b[i]));
    return worst;
}

double rms (const std::vector<float>& x)
{
    double sum = 0.0;
    for (float v : x)
        sum += (double) v * v;
    return std::sqrt (sum / (double) std::max<size_t> (1, x.size()));
}

// The engine's delay: per block the loop's raw values, then the node; COLOR and SPREAD every 10 ms.
struct DelayRig
{
    explicit DelayRig (const juce::var& parameters)
    {
        node = a3::engine::createNode (a3::IemKind::dualDelay, busChannels, busChannels, "fx");
        a3::engine::require (node->setParameters (parameters), "fx");
        loop = std::make_unique<a3::DelayLoop> (*node);
        shape = std::make_unique<a3::DelayShape> (*node);
        node->prepare (rate, block);
        loop->prepare (rate);
        for (int tick = 0; tick < 20; ++tick)   // glide to the spread before the first block
            shape->tick (a3::delay::defaultColor, 45.0f, *node);
    }

    Channels render (const Channels& in)
    {
        const size_t frames = in[0].size();
        Channels out ((size_t) busChannels, std::vector<float> (frames));
        juce::AudioBuffer<float> io (busChannels, block);
        for (size_t start = 0; start + block <= frames; start += block)
        {
            for (int ch = 0; ch < busChannels; ++ch)
                io.copyFrom (ch, 0, in[(size_t) ch].data() + start, block);
            loop->set (120.0f, a3::delay::defaultBeat, 0.8f, block);
            a3::engine::runNode (*node, io, block);
            for (int ch = 0; ch < busChannels; ++ch)
                std::copy_n (io.getReadPointer (ch), block, out[(size_t) ch].begin() + (long) start);
        }
        return out;
    }

    std::unique_ptr<a3::IemNode> node;
    std::unique_ptr<a3::DelayLoop> loop;
    std::unique_ptr<a3::DelayShape> shape;
};

juce::var atThirdOrder (const juce::var& parameters, const char* orderId)
{
    auto copy = parameters.clone();
    copy.getDynamicObject()->setProperty (orderId, a3::iemOrderThree);
    return copy;
}
}

TEST (ReducedOrder, TheHeadphoneDecodersReadOnlyTheFirstNineChannels)
{
    const auto config = shippedConfig();
    const auto in = noise ((size_t) block * 200, 7);
    for (const auto& [name, parameters] : { std::pair { "rec", config.recParameters }, { "phones", config.phonesParameters } })
    {
        a3::HeadphoneDecoder full (parameters, name), cut (parameters, name);
        full.prepare (rate, block);
        cut.prepare (rate, block);
        const auto withAll = decode (full, in);
        const auto withNine = decode (cut, withoutThirdOrder (in));
        for (int side = 0; side < 2; ++side)
        {
            ASSERT_GT (rms (withNine[(size_t) side]), 1e-3) << name;
            EXPECT_EQ (largestDifference (withAll[(size_t) side], withNine[(size_t) side]), 0.0) << name << " side " << side;
        }
    }
}

// The delay at 2nd order equals the 3rd-order delay fed ACN 0..8 only, through BEAT, FEEDBK and a
// SPREAD of 45 degrees (rotation keeps each order to itself, so nothing leaks between them).
TEST (ReducedOrder, TheDelayEqualsTheThirdOrderDelayFedOnlyTheFirstNineChannels)
{
    const auto config = shippedConfig();
    const auto in = noise ((size_t) block * 2400, 11);   // 3.5 s: echoes at 0.5 s, fed back
    DelayRig secondOrder (config.fxParameters), thirdOrder (atThirdOrder (config.fxParameters, "orderSetting"));
    const auto reduced = secondOrder.render (in);
    const auto reference = thirdOrder.render (withoutThirdOrder (in));

    for (int ch = 0; ch < secondOrderChannels; ++ch)
    {
        const auto& r = reference[(size_t) ch];
        ASSERT_GT (rms (r), 1e-3) << "ACN " << ch;
        EXPECT_LT (largestDifference (reduced[(size_t) ch], r), 1e-5) << "ACN " << ch;
    }
    for (int ch = secondOrderChannels; ch < busChannels; ++ch)
    {
        EXPECT_EQ (rms (reduced[(size_t) ch]), 0.0) << "ACN " << ch << " leaves the 2nd-order delay";
        EXPECT_LT (rms (reference[(size_t) ch]), 1e-6) << "ACN " << ch << " of the reference";
    }
}

// What the order costs in level: a plane wave from the front and from the side, decoded at 3rd
// order and at 2nd order (IEM's own filters for each). Printed for the record; a gross change fails.
TEST (ReducedOrder, TheSecondOrderHeadphoneLevelStaysWithinThreeDecibels)
{
    const auto config = shippedConfig();
    for (const float azimuth : { 0.0f, 90.0f, 135.0f })
    {
        auto encoder = a3::engine::createNode (a3::IemKind::multiEncoder, 1, busChannels, "encoder");
        a3::engine::require (encoder->setParameters (juce::JSON::parse (
                                 R"({"inputSetting":1,"orderSetting":4,"useSN3D":1,"azimuth0":)" + juce::String (azimuth) + "}")),
                             "encoder");
        encoder->prepare (rate, block);
        const size_t frames = (size_t) block * 400;
        const auto mono = noise (frames, 3)[0];
        Channels bus ((size_t) busChannels, std::vector<float> (frames));
        juce::AudioBuffer<float> io (busChannels, block);
        for (size_t start = 0; start + block <= frames; start += block)
        {
            io.clear();
            io.copyFrom (0, 0, mono.data() + start, block);
            a3::engine::runNode (*encoder, io, block);
            for (int ch = 0; ch < busChannels; ++ch)
                std::copy_n (io.getReadPointer (ch), block, bus[(size_t) ch].begin() + (long) start);
        }

        const auto thirdOrder = atThirdOrder (config.phonesParameters, "inputOrderSetting");
        a3::HeadphoneDecoder reduced (config.phonesParameters, "phones"), third (thirdOrder, "phones-3rd"),
            cut (thirdOrder, "phones-3rd-cut");
        for (auto* decoder : { &reduced, &third, &cut })
            decoder->prepare (rate, block);
        const auto a = decode (reduced, bus), b = decode (third, bus), c = decode (cut, withoutThirdOrder (bus));
        const auto db = [] (const std::vector<float>& x, const std::vector<float>& y) { return 20.0 * std::log10 (rms (x) / rms (y)); };
        for (int side = 0; side < 2; ++side)
        {
            const auto s = (size_t) side;
            std::printf ("[ measured ] azimuth %5.1f side %d: 2nd vs 3rd order %+.2f dB (3rd order cut to ACN 0..8: %+.2f dB)\n",
                         azimuth, side, db (a[s], b[s]), db (c[s], b[s]));
            EXPECT_LT (std::abs (db (a[s], b[s])), 3.0) << "azimuth " << azimuth << " side " << side;
        }
    }
}
