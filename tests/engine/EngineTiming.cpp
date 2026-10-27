// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include "AmbiEncoderBus.h"
#include "DeckStrip.h"
#include "EngineFixture.h"
#include "EngineStages.h"
#include "VuTestData.h"

// Regression guards, not the acceptance: the rig's 10-minute worst case decides (Settled 5).
// Wall-clock numbers only mean something on a quiet machine (run under the build lock).
using namespace a3test;

namespace
{
constexpr int quantum = 128;
constexpr size_t loopFrames = 128 * 344;   // about 1 s of signal, cycled
constexpr int warmUpBlocks = 200;

using Clock = std::chrono::steady_clock;

double msSince (Clock::time_point t0) { return std::chrono::duration<double, std::milli> (Clock::now() - t0).count(); }

struct Timing
{
    double meanMs = 0.0, worstMs = 0.0;
};

// Times `blocks` quantum-sized process calls; inputs and outputs are prepared outside the clock.
Timing timeEngine (EngineRig& rig, int blocks)
{
    std::vector<a3::EngineInputs> ins;
    std::vector<a3::EngineOutputs> outs;
    for (size_t start = 0; start + quantum <= rig.frames; start += quantum)
    {
        ins.push_back (rig.inputsAt (start));
        outs.push_back (rig.outputsAt (start));
    }
    Timing t;
    double total = 0.0;
    for (int b = -warmUpBlocks; b < blocks; ++b)
    {
        const size_t i = (size_t) (b + warmUpBlocks) % ins.size();
        const auto t0 = Clock::now();
        rig.engine->process (ins[i], outs[i], quantum, rig.controls);
        const double ms = msSince (t0);
        if (b < 0)
            continue;
        total += ms;
        t.worstMs = std::max (t.worstMs, ms);
    }
    t.meanMs = total / blocks;
    return t;
}

// Times one piece of the graph on its own (`work` runs one quantum).
double meanMsOf (int blocks, const std::function<void()>& work)
{
    for (int b = 0; b < warmUpBlocks; ++b)
        work();
    const auto t0 = Clock::now();
    for (int b = 0; b < blocks; ++b)
        work();
    return msSince (t0) / blocks;
}

void deckOnEveryChannel (EngineRig& rig)
{
    for (size_t d = 0; d < rig.decks.size(); ++d)
    {
        for (int ch = 0; ch < deckChannels; ++ch)
            rig.decks[d].bformat[(size_t) ch] = noise (rig.frames, 0.1f, 500 + 20 * (uint32_t) d + (uint32_t) ch).l;
        rig.decks[d].cue = { noise (rig.frames, 0.1f, 600 + (uint32_t) d).l, noise (rig.frames, 0.1f, 610 + (uint32_t) d).l };
    }
}

void analogOnEveryPair (EngineRig& rig)
{
    for (int c = 0; c < rig.config.channels; ++c)
        if (const int pair = rig.config.analogPairs[(size_t) c]; pair > 0)
            rig.setAnalogPair (pair, pinkStereo (rig.frames, (uint32_t) pair));
}

// Everything that costs: aux into the delay, cue and mix into the phones, the filter on.
void everySendOpen (EngineRig& rig)
{
    for (int c = 0; c < rig.config.channels; ++c)
    {
        auto& ch = rig.controls.channel (c);
        ch.auxSend = 0.5f;
        ch.cue = 1.0f;
        ch.filterOn = 1.0f;
    }
    rig.controls.global().phonesMix = 0.5f;
}

// The pieces of the N = 4 graph timed alone, as shares of each whole (printed only). On
// decks the analog strips do not run (R14); their encoders still do, with silent sources.
void printShares (const a3::SpatialConfig& spatial, double decksMs, double analogMs, int blocks)
{
    const auto in = noise (quantum, 0.1f, 700);
    std::vector<float> scratch ((size_t) (16 * quantum));
    const float* stereoIn[2] { in.l.data(), in.r.data() };

    std::vector<std::vector<float>> planes (10, std::vector<float> (quantum));
    a3::StripBuffers taps { { planes[0].data(), planes[1].data() }, { planes[2].data(), planes[3].data() },
                            { planes[4].data(), planes[5].data() }, { planes[6].data(), planes[7].data() },
                            { planes[8].data(), planes[9].data() } };
    a3::FilterControls filter;
    a3::ChannelStrip strip;
    strip.setSharedFilter (&filter);
    strip.prepare (engineRate, quantum);
    strip.controls().filterOn = 1.0f;

    a3::DeckStrip deck;
    deck.setSharedFilter (&filter);
    deck.prepare (engineRate, quantum);
    a3::StripControls deckControls;
    deckControls.filterOn = 1.0f;
    std::vector<std::vector<float>> deckPlanes (32, std::vector<float> (quantum));
    const float* deckIn[16];
    a3::DeckTaps deckTaps;
    for (int k = 0; k < 16; ++k)
    {
        deckIn[k] = in.l.data();
        deckTaps.preVolume[k] = deckPlanes[(size_t) k].data();
        deckTaps.postVolume[k] = deckPlanes[(size_t) k + 16].data();
    }

    juce::String error;
    auto encoder = a3::AmbiEncoderBus::create (4, spatial, error);
    encoder->prepare (engineRate, quantum);
    const std::vector<a3::StereoIn> sources (4, { in.l.data(), in.r.data() });
    std::vector<std::atomic<float>> azimuth (4), elevation (4);
    juce::AudioBuffer<float> bus (16, quantum);

    auto delay = a3::engine::createNode (a3::IemKind::dualDelay, 16, 16, "fx");
    delay->setParameters (spatial.fxParameters);
    delay->prepare (engineRate, quantum);
    a3::ZoneDecoder zone (spatial.main, "main");
    zone.prepare (engineRate, quantum);
    zone.setLevel (masterAtRest);
    const a3::ZoneOutputs nowhere;
    a3::OutputGate gate;
    gate.prepare (engineRate);
    gate.setTrim (-30.88f);
    gate.startAt (1.0f);
    a3::HeadphoneDecoder binaural (spatial.recParameters, "rec");
    binaural.prepare (engineRate, quantum);
    const auto fillBus = [&]
    {
        for (int ch = 0; ch < 16; ++ch)
            bus.copyFrom (ch, 0, in.l.data(), quantum);
    };

    const double perStrip = meanMsOf (blocks, [&] { strip.process (stereoIn, taps, quantum); });
    const double perDeck = meanMsOf (blocks, [&] { deck.process (deckIn, deckTaps, quantum, deckControls); });
    const double perEncoder = meanMsOf (blocks, [&] { encoder->process (sources, sources, sources[0], bus, quantum, azimuth.data(), elevation.data()); });
    const double perDelay = meanMsOf (blocks, [&] { fillBus(); a3::engine::runNode (*delay, bus, quantum); });
    const double perZone = meanMsOf (blocks, [&] { fillBus(); zone.process (bus, nowhere, 0, quantum, gate, true); });
    const double perBinaural = meanMsOf (blocks, [&] { fillBus(); binaural.process (bus, quantum); });

    const auto row = [&] (const char* what, int onDecks, int onAnalog, double each)
    {
        std::printf ("  %-28s %.4f ms each | decks %d x = %4.1f %% | analog %d x = %4.1f %%\n", what, each, onDecks,
                     100.0 * onDecks * each / decksMs, onAnalog, 100.0 * onAnalog * each / analogMs);
    };
    std::printf ("EngineTiming shares (each piece timed alone; whole graph: decks %.4f ms, analog %.4f ms):\n", decksMs, analogMs);
    row ("analog strip (filter on)", 0, 4, perStrip);
    row ("deck strip, 16 ch", 4, 0, perDeck);
    row ("AmbiEncoderBus, 4 sources", 3, 3, perEncoder);
    row ("DualDelay", 1, 1, perDelay);
    row ("SimpleDecoder + MASTER/gate", 2, 2, perZone);
    row ("BinauralDecoder", 2, 2, perBinaural);
}

double loadAverage()
{
    double load[1] {};
    return getloadavg (load, 1) == 1 ? load[0] : -1.0;
}
}

// N = 4, quantum 128. The worst case of the revision: all four channels on decks (with signal
// on the analog pairs too), two stem cues; then the same with all four analog. Judged on the
// best of several repetition means: background load only ever adds time, so it cannot fail
// the guard, while a regression slows every repetition.
TEST (EngineTiming, FullGraphUnderOneMillisecondPer128)
{
#ifndef NDEBUG
    GTEST_SKIP() << "timing is measured in Release only";
#endif
    constexpr int blocks = 2000;
    constexpr int repetitions = 5;
    constexpr double budgetMs = 1.0;

    EngineRig decks (engineConfig (4, 2), loopFrames, quantum);
    deckOnEveryChannel (decks);
    analogOnEveryPair (decks);
    everySendOpen (decks);
    for (int c = 0; c < 4; ++c)
        decks.controls.channel (c).source = c < 2 ? 1 : 2;

    EngineRig analog (engineConfig (4, 2), loopFrames, quantum);
    deckOnEveryChannel (analog);   // the stem cues still run
    analogOnEveryPair (analog);
    everySendOpen (analog);

    // The /vu taps (O13) are part of the graph's cost since Task 24.
    a3::VuTaps decksTaps (truthVuMeters(), a3::VuSettings {}), analogTaps (truthVuMeters(), a3::VuSettings {});
    decks.engine->attachVuTaps (decksTaps);
    analog.engine->attachVuTaps (analogTaps);

    double bestDecks = 1e9, bestAnalog = 1e9;
    for (int r = 0; r < repetitions; ++r)
    {
        const auto onDecks = timeEngine (decks, blocks);
        const auto onAnalog = timeEngine (analog, blocks);
        std::printf ("EngineTiming rep %d (load %.2f): all decks mean_ms %.4f worst_ms %.4f; all analog mean_ms %.4f worst_ms %.4f\n",
                     r + 1, loadAverage(), onDecks.meanMs, onDecks.worstMs, onAnalog.meanMs, onAnalog.worstMs);
        bestDecks = std::min (bestDecks, onDecks.meanMs);
        bestAnalog = std::min (bestAnalog, onAnalog.meanMs);
    }
    std::printf ("EngineTiming best of %d: all decks %.4f ms, all analog %.4f ms (budget %.1f)\n", repetitions, bestDecks,
                 bestAnalog, budgetMs);
    RecordProperty ("decks_best_mean_ms", std::to_string (bestDecks));
    RecordProperty ("analog_best_mean_ms", std::to_string (bestAnalog));
    printShares (decks.spatial, bestDecks, bestAnalog, blocks);

    EXPECT_LT (bestDecks, budgetMs);
    EXPECT_LT (bestAnalog, budgetMs);
}

// No channel cap (D11): what one more channel costs, at N = 32 against N = 4.
TEST (EngineTiming, ReportsCostPerChannelAtThirtyTwo)
{
    constexpr int blocks = 500;
    const auto config = [] (int channels, bool analog)
    {
        auto c = engineConfig (channels, 2);
        c.analogInputs = 2 * channels + 2;
        c.returnAnalogPair = channels + 1;
        for (int k = 0; k < channels; ++k)
            c.analogPairs[(size_t) k] = analog ? k + 1 : 0;
        return c;
    };
    const auto meanMs = [&] (int channels, bool analog)
    {
        EngineRig rig (config (channels, analog), 128 * 64, quantum);
        deckOnEveryChannel (rig);
        if (analog)
            analogOnEveryPair (rig);
        everySendOpen (rig);
        for (int c = 0; c < channels && ! analog; ++c)
            rig.controls.channel (c).source = 1 + c % 2;
        return timeEngine (rig, blocks).meanMs;
    };
    const double analogUs = 1000.0 * (meanMs (32, true) - meanMs (4, true)) / 28.0;
    const double deckUs = 1000.0 * (meanMs (32, false) - meanMs (4, false)) / 28.0;
    std::printf ("EngineTiming (load %.2f): per analog channel %.2f us, per deck channel %.2f us (quantum 128)\n",
                 loadAverage(), analogUs, deckUs);
    RecordProperty ("analog_channel_us", std::to_string (analogUs));
    RecordProperty ("deck_channel_us", std::to_string (deckUs));
    EXPECT_TRUE (std::isfinite (analogUs));
    EXPECT_TRUE (std::isfinite (deckUs));
    EXPECT_GT (analogUs, 0.0);
    EXPECT_GT (deckUs, 0.0);
}
