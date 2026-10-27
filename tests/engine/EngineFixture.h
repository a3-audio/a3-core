// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
#include "Engine.h"
#include "SpatialBusFixture.h"

// One engine over whole signals: inputs as vectors (empty = nullptr = silence),
// every output captured. Trims are 0 dB and, unless asked otherwise, the gate
// starts open at them (startGateAt), so the graph's tests see no 1 s fade-in.
namespace a3test
{
constexpr double engineRate = 44100.0;
constexpr int engineBlock = 64;
constexpr int deckChannels = 16;
constexpr float masterAtRest = 0.680352f;   // PurestGain -12.786 dB, the template's dec_master
constexpr float boothAtRest = 0.996100f;    // -0.156 dB, dec_booth

// Channel c on analog pair c+1 while there are pairs below the return (pair 6), deck-only above.
inline a3::EngineConfig engineConfig (int channels = 4, int decks = 2)
{
    a3::EngineConfig config;
    config.channels = channels;
    config.decks = decks;
    config.analogPairs.assign ((size_t) channels, 0);
    for (int c = 0; c < channels && c + 1 < config.returnAnalogPair; ++c)
        config.analogPairs[(size_t) c] = c + 1;
    config.trims.main = config.trims.booth = config.trims.phones = 0.0f;
    return config;
}

struct DeckSignals
{
    std::array<std::vector<float>, deckChannels> bformat, aux;
    std::array<std::vector<float>, 9> cue;   // 2nd-order B-format, ACN 0..8
};

inline const float* pointerAt (const std::vector<float>& signal, size_t start)
{
    return signal.empty() ? nullptr : signal.data() + start;
}

inline std::vector<float> mono (size_t frames) { return std::vector<float> (frames, 0.0f); }

class EngineRig
{
public:
    EngineRig (a3::EngineConfig engineConfiguration, size_t totalFrames, int blockSize = engineBlock,
               double sampleRate = engineRate, bool gateOpenAtStart = true)
        : config (std::move (engineConfiguration)),
          spatial (loadedSpatialConfig (shippedSpatialDirectory)),
          controls (config.channels),
          analog ((size_t) config.analogInputs),
          decks ((size_t) config.decks),
          frames (totalFrames),
          out (spatial, totalFrames),
          phones (silentStereo (totalFrames)),
          pre ((size_t) config.channels, silentStereo (totalFrames)),
          post ((size_t) config.channels, silentStereo (totalFrames))
    {
        juce::String error;
        engine = a3::Engine::create (config, spatial, error);
        if (engine == nullptr)
            throw std::runtime_error (error.toStdString());
        engine->prepare (sampleRate, blockSize);
        if (gateOpenAtStart)
            engine->startGateAt (fullyOpen (config.trims));
        restValues();
    }

    void restValues()
    {
        for (int c = 0; c < controls.channels(); ++c)
        {
            auto& ch = controls.channel (c);
            ch.gain = 0.75f; ch.volume = 0.75f;
            ch.eqHigh = ch.eqMid = ch.eqLow = 0.5f;
            ch.frequency = 0.5f; ch.q = 0.5f; ch.threeD = 1.0f;
            ch.source = 0; ch.cue = 0.0f; ch.auxSend = 0.0f; ch.filterOn = 0.0f;
            const auto& place = staticPlacement[(size_t) c % staticPlacement.size()];
            ch.azimuth = place.first; ch.elevation = place.second;
        }
        auto& g = controls.global();
        g.masterVolume = masterAtRest; g.booth = boothAtRest;
        g.phonesMix = 0.0f; g.phonesVolume = 0.75f; g.auxReturn = 0.75f;
        g.returnSource = 0; g.returnCue = 0.0f; g.delayBpm = 120.0f; g.gateOpen = true;
    }

    // Deck strip at unity: TRIM and VOLUME 0 dB, EQ flat.
    void neutralStrip (int channel)
    {
        auto& ch = controls.channel (channel);
        ch.gain = 1.0f; ch.volume = 1.0f;
        ch.eqHigh = ch.eqMid = ch.eqLow = 0.5f;
    }

    void setAnalogPair (int pair, const Stereo& signal)
    {
        analog[(size_t) (2 * pair - 2)] = signal.l;
        analog[(size_t) (2 * pair - 1)] = signal.r;
    }

    // Runs [begin, end) with call sizes cycling through `calls`; `beforeCall (start)` may change controls.
    void run (size_t begin = 0, size_t end = 0, const std::vector<int>& calls = { engineBlock },
              const std::function<void (size_t)>& beforeCall = {}, const std::function<void()>& afterCall = {})
    {
        if (end == 0)
            end = frames;
        size_t k = 0;
        for (size_t start = begin; start < end;)
        {
            const int n = (int) std::min<size_t> ((size_t) calls[k++ % calls.size()], end - start);
            if (beforeCall)
                beforeCall (start);
            engine->process (inputsAt (start), outputsAt (start), n, controls);
            if (afterCall)
                afterCall();
            start += (size_t) n;
        }
    }

    // Every captured output channel, in a fixed order (rec, main, booth, phones, pre, post).
    std::vector<const std::vector<float>*> everyOutput() const
    {
        std::vector<const std::vector<float>*> all;
        for (const auto& channel : out.channels)
            all.push_back (&channel);
        for (const auto* stereo : { &phones })
            all.insert (all.end(), { &stereo->l, &stereo->r });
        for (const auto& s : pre)
            all.insert (all.end(), { &s.l, &s.r });
        for (const auto& s : post)
            all.insert (all.end(), { &s.l, &s.r });
        return all;
    }

    // The gate at each zone's trim (closed where a zone has none).
    static a3::GateLevels fullyOpen (const a3::Trims& trims)
    {
        a3::GateLevels levels;
        const std::optional<float> zones[] { trims.main, trims.booth, trims.phones };
        for (int z = 0; z < 3; ++z)
            levels.gain[z] = zones[z].has_value() ? juce::Decibels::decibelsToGain (*zones[z], -1000.0f) : 0.0f;
        return levels;
    }

    std::vector<const std::vector<float>*> mainOutputs() const { return channelsAt (out.mainIndices); }
    std::vector<const std::vector<float>*> boothOutputs() const { return channelsAt (out.boothIndices); }
    std::vector<const std::vector<float>*> recOutputs() const { return channelsAt (out.recIndices); }

    a3::EngineConfig config;
    a3::SpatialConfig spatial;
    std::unique_ptr<a3::Engine> engine;
    a3::EngineControls controls;
    std::vector<std::vector<float>> analog;
    std::vector<DeckSignals> decks;
    bool omitDecks = false;   // hand the engine no deck inputs at all
    size_t frames;
    OutputSignals out;
    Stereo phones;
    std::vector<Stereo> pre, post;

    a3::EngineInputs inputsAt (size_t start) const
    {
        a3::EngineInputs in;
        for (const auto& signal : analog)
            in.analog.push_back (pointerAt (signal, start));
        if (omitDecks)
            return in;
        for (const auto& deck : decks)
        {
            a3::DeckInputs d;
            for (int ch = 0; ch < deckChannels; ++ch)
                d.bformat[(size_t) ch] = pointerAt (deck.bformat[(size_t) ch], start);
            for (int ch = 0; ch < deckChannels; ++ch)
                d.aux[(size_t) ch] = pointerAt (deck.aux[(size_t) ch], start);
            for (size_t acn = 0; acn < d.cue.size(); ++acn)
                d.cue[acn] = pointerAt (deck.cue[acn], start);
            in.decks.push_back (d);
        }
        return in;
    }

    a3::EngineOutputs outputsAt (size_t start)
    {
        const auto spatialOut = out.at (start);
        a3::EngineOutputs o;
        o.main = spatialOut.main;
        o.booth = spatialOut.booth;
        o.rec[0] = spatialOut.rec[0];
        o.rec[1] = spatialOut.rec[1];
        o.phones[0] = phones.l.data() + start;
        o.phones[1] = phones.r.data() + start;
        for (size_t c = 0; c < pre.size(); ++c)
        {
            o.pre.push_back ({ pre[c].l.data() + start, pre[c].r.data() + start });
            o.post.push_back ({ post[c].l.data() + start, post[c].r.data() + start });
        }
        return o;
    }

private:
    std::vector<const std::vector<float>*> channelsAt (const std::vector<size_t>& indices) const
    {
        std::vector<const std::vector<float>*> list;
        for (auto i : indices)
            list.push_back (&out.channels[i]);
        return list;
    }
};

inline double energy (const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        sum += (double) x[i] * x[i];
    return sum;
}

inline double energy (const std::vector<const std::vector<float>*>& channels, size_t from, size_t to)
{
    double sum = 0.0;
    for (const auto* channel : channels)
        sum += energy (*channel, from, to);
    return sum;
}

inline double peak (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    double worst = 0.0;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        worst = std::max (worst, (double) std::abs (x[i]));
    return worst;
}

inline double peak (const std::vector<const std::vector<float>*>& channels, size_t from = 0, size_t to = SIZE_MAX)
{
    double worst = 0.0;
    for (const auto* channel : channels)
        worst = std::max (worst, peak (*channel, from, to));
    return worst;
}

inline double maxDifference (const std::vector<const std::vector<float>*>& a, const std::vector<const std::vector<float>*>& b,
                             size_t from = 0)
{
    double worst = 0.0;
    for (size_t c = 0; c < std::min (a.size(), b.size()); ++c)
        for (size_t i = from; i < a[c]->size(); ++i)
            worst = std::max (worst, std::abs ((double) (*a[c])[i] - (double) (*b[c])[i]));
    return worst;
}

inline double maxStep (const std::vector<float>& x, size_t from = 1)
{
    double worst = 0.0;
    for (size_t i = std::max<size_t> (from, 1); i < x.size(); ++i)
        worst = std::max (worst, (double) std::abs (x[i] - x[i - 1]));
    return worst;
}

inline std::vector<float> impulseAt (size_t frames, size_t where, float level = 1.0f)
{
    std::vector<float> x (frames, 0.0f);
    x[where] = level;
    return x;
}
}
