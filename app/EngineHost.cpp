// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineHost.h"
#include <algorithm>

namespace a3app
{
namespace
{
bool silent (const a3::GateLevels& levels)
{
    return std::all_of (std::begin (levels.gain), std::end (levels.gain), [] (float g) { return g == 0.0f; });
}

void clear (float* channel, int n)
{
    if (channel != nullptr)
        juce::FloatVectorOperations::clear (channel, n);
}

void silence (const a3::EngineOutputs& out, int n)
{
    for (const auto* zone : { &out.main, &out.booth })
    {
        clear (zone->sub, n);
        for (auto* top : zone->tops)
            clear (top, n);
    }
    for (int side = 0; side < 2; ++side)
    {
        clear (out.rec[side], n);
        clear (out.phones[side], n);
    }
    for (const auto* taps : { &out.pre, &out.post })
        for (const auto& pair : *taps)
            for (auto* channel : pair)
                clear (channel, n);
}
}

EngineHost::EngineHost (a3::EngineConfig c, a3::SpatialConfig s, const EngineTruth& truth,
                        std::function<void (const juce::OSCMessage&)> sendToCore)
    : config (std::move (c)), spatial (std::move (s)), wire (config.channels), hello (truth, std::move (sendToCore))
{
}

EngineHost::~EngineHost()
{
    delete current.exchange (nullptr);
}

juce::Result EngineHost::rebuild (double sampleRate, int blockSize, bool fadeFirst)
{
    freeWhatNoCallbackSees();
    juce::String error;
    auto next = a3::Engine::create (config, spatial, error);
    if (next == nullptr)
        return juce::Result::fail (error);
    next->prepare (sampleRate, blockSize);   // closes its gate

    auto* running = current.load();
    if (running != nullptr && fadeFirst)
    {
        running->holdGateClosed (true);
        waiting = std::move (next);          // controlTick swaps it in once the running one is silent
        return juce::Result::ok();
    }
    waiting.reset();                         // a waiting fade is overtaken; it was never published
    if (running != nullptr)
        next->startGateAt (running->gateLevels());
    publish (std::move (next));
    return juce::Result::ok();
}

void EngineHost::audioCallback (const a3::EngineInputs& in, const a3::EngineOutputs& out, int numSamples)
{
    begun.fetch_add (1);
    if (auto* engine = current.load())
        engine->process (in, out, numSamples, wire);
    else
        silence (out, numSamples);
    ended.fetch_add (1);
}

void EngineHost::controlTick (double nowSeconds)
{
    freeWhatNoCallbackSees();
    if (waiting != nullptr && current.load() != nullptr && silent (current.load()->gateLevels()))
        publish (std::move (waiting));
    if (auto* engine = current.load())
    {
        engine->serviceControls (nowSeconds);
        lastMeters = engine->outputMeters();
    }
    hello.tick (nowSeconds);
}

a3::EngineControls& EngineHost::controls() { return wire; }
juce::String EngineHost::bootId() const { return hello.bootId(); }
const a3::EngineConfig& EngineHost::engineConfig() const { return config; }
const a3::SpatialConfig& EngineHost::spatialConfig() const { return spatial; }
std::vector<a3::Engine::OutputMeter> EngineHost::meters() const { return lastMeters; }

a3::GateLevels EngineHost::gateLevels() const
{
    const auto* engine = current.load();
    return engine != nullptr ? engine->gateLevels() : a3::GateLevels {};
}

int EngineHost::preparedBlockSize() const
{
    const auto* engine = current.load();
    return engine != nullptr ? engine->preparedBlockSize() : 0;
}

a3::Engine* EngineHost::engine() { return current.load(); }

// The replaced Engine is kept until every callback that began before the exchange has
// ended: a callback that begins after it loads the new one (both counters and the
// pointer are sequentially consistent).
void EngineHost::publish (std::unique_ptr<a3::Engine> next)
{
    if (auto* replaced = current.exchange (next.release()))
        retired.push_back ({ std::unique_ptr<a3::Engine> (replaced), begun.load() });
}

void EngineHost::freeWhatNoCallbackSees()
{
    const auto done = ended.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [done] (const Retired& r) { return done >= r.callbacksBegun; }),
                   retired.end());
}
}
