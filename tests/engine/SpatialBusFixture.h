// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include "ChannelStrip.h"
#include "SpatialBus.h"
#include "SpatialTestData.h"
#include "TestSignals.h"

// Four real strips at one fixed point feeding the bus, and storage for every
// spatial output the config has (rec L/R, then each zone's sub and tops).
namespace a3test
{
constexpr double spatialRate = 44100.0;
constexpr int spatialQuantum = 64;

inline a3::SpatialConfig loadedSpatialConfig (const juce::File& directory)
{
    juce::String error;
    auto config = a3::loadSpatialConfig (directory, error);
    if (! config)
        throw std::runtime_error (error.toStdString());
    return *config;
}

// gain 0.75, volume 0.75, EQ 0.5, frequency 0.5, q 0.5, 3D 1.
inline std::unique_ptr<a3::ChannelStrip> stripAtTheFixedPoint()
{
    auto strip = std::make_unique<a3::ChannelStrip>();
    strip->prepare (spatialRate, 512);
    auto& c = strip->controls();
    c.gain = 0.75f; c.volume = 0.75f;
    c.eqHigh = c.eqMid = c.eqLow = 0.5f;
    c.frequency = 0.5f; c.q = 0.5f; c.threeD = 1.0f;
    return strip;
}

using StripInputs = std::array<Stereo, a3::numSpatialStrips>;

inline Stereo silentStereo (size_t frames) { return { std::vector<float> (frames), std::vector<float> (frames) }; }

inline Stereo pinkStereo (size_t frames, uint32_t seed)
{
    return { pink (frames, 0.25f, seed), pink (frames, 0.25f, seed + 100) };
}

inline StripInputs pinkOnAllStrips (size_t frames)
{
    return { pinkStereo (frames, 1), pinkStereo (frames, 2), pinkStereo (frames, 3), pinkStereo (frames, 4) };
}

inline StripInputs pinkOnOneStrip (size_t frames, size_t strip)
{
    StripInputs inputs { silentStereo (frames), silentStereo (frames), silentStereo (frames), silentStereo (frames) };
    inputs[strip] = pinkStereo (frames, 1);
    return inputs;
}

inline StripInputs silentStrips (size_t frames)
{
    return { silentStereo (frames), silentStereo (frames), silentStereo (frames), silentStereo (frames) };
}

struct TapSignals
{
    std::array<Stereo, a3::numSpatialStrips> moving, steady;
    size_t frames() const { return moving[0].l.size(); }

    std::array<a3::StripTaps, a3::numSpatialStrips> at (size_t start) const
    {
        std::array<a3::StripTaps, a3::numSpatialStrips> taps {};
        for (size_t s = 0; s < taps.size(); ++s)
            taps[s] = { { moving[s].l.data() + start, moving[s].r.data() + start },
                        { steady[s].l.data() + start, steady[s].r.data() + start } };
        return taps;
    }
};

inline TapSignals renderStrips (const StripInputs& inputs)
{
    TapSignals taps;
    for (size_t s = 0; s < inputs.size(); ++s)
    {
        const size_t frames = inputs[s].l.size();
        taps.moving[s] = taps.steady[s] = silentStereo (frames);
        auto strip = stripAtTheFixedPoint();
        for (size_t start = 0; start < frames; start += spatialQuantum)
        {
            const int n = (int) std::min<size_t> (spatialQuantum, frames - start);
            const float* in[2] { inputs[s].l.data() + start, inputs[s].r.data() + start };
            float* m[2] { taps.moving[s].l.data() + start, taps.moving[s].r.data() + start };
            float* st[2] { taps.steady[s].l.data() + start, taps.steady[s].r.data() + start };
            strip->process (in, m, st, n);
        }
    }
    return taps;
}

class OutputSignals
{
public:
    OutputSignals (const a3::SpatialConfig& config, size_t frames) : length (frames)
    {
        recIndices = { add(), add() };
        mainIndices = addZone (config.main.layout, mainSub, mainTops);
        boothIndices = addZone (config.booth.layout, boothSub, boothTops);
    }

    a3::SpatialOutputs at (size_t start)
    {
        a3::SpatialOutputs out;
        out.rec[0] = channels[recIndices[0]].data() + start;
        out.rec[1] = channels[recIndices[1]].data() + start;
        out.main = zoneAt (mainSub, mainTops, start);
        out.booth = zoneAt (boothSub, boothTops, start);
        return out;
    }

    std::vector<std::vector<float>> channels;
    std::vector<size_t> recIndices, mainIndices, boothIndices;

private:
    static constexpr int none = -1;

    size_t add()
    {
        channels.emplace_back (length, 0.0f);
        return channels.size() - 1;
    }

    std::vector<size_t> addZone (const a3::Layout& layout, int& sub, std::vector<size_t>& tops)
    {
        std::vector<size_t> all;
        sub = none;
        if (layout.hasSub())
        {
            sub = (int) add();
            all.push_back ((size_t) sub);
        }
        for (size_t k = 0; k < layout.topChannels.size(); ++k)
        {
            tops.push_back (add());
            all.push_back (tops.back());
        }
        return all;
    }

    a3::ZoneOutputs zoneAt (int sub, const std::vector<size_t>& tops, size_t start)
    {
        a3::ZoneOutputs zone;
        zone.sub = sub == none ? nullptr : channels[(size_t) sub].data() + start;
        for (const auto index : tops)
            zone.tops.push_back (channels[index].data() + start);
        return zone;
    }

    size_t length;
    int mainSub = none, boothSub = none;
    std::vector<size_t> mainTops, boothTops;
};

using Placement = std::array<std::pair<float, float>, a3::numSpatialStrips>;   // azimuth, elevation per strip

inline void place (a3::SpatialControls& controls, const Placement& placement)
{
    for (size_t i = 0; i < placement.size(); ++i)
    {
        controls.azimuth[i] = placement[i].first;
        controls.elevation[i] = placement[i].second;
    }
}

inline const Placement staticPlacement { { { 30.0f, 10.0f }, { -60.0f, 0.0f }, { 120.0f, 20.0f }, { -150.0f, -10.0f } } };

// Runs the bus over [begin, end) of the taps with call sizes cycling through `calls`.
inline void runBus (a3::SpatialBus& bus, const TapSignals& taps, OutputSignals& out, const std::vector<int>& calls,
                    const a3::SpatialControls& positions, size_t begin = 0, size_t end = 0)
{
    if (end == 0)
        end = taps.frames();
    size_t k = 0;
    for (size_t start = begin; start < end;)
    {
        const int n = (int) std::min<size_t> ((size_t) calls[k++ % calls.size()], end - start);
        bus.process (taps.at (start), out.at (start), n, positions);
        start += (size_t) n;
    }
}
}
