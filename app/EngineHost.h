// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "Engine.h"
#include "HelloSender.h"
#include "OscVocabulary.h"

namespace a3app
{
// Hosts the Engine for a backend. Owns the controls (they outlive every graph), the
// current Engine and its replacement, and the hello.
//
// Threads: audioCallback runs on the audio thread; everything else on one control
// thread (the app's 50 Hz timer). The audio thread only loads the current Engine; the
// control thread publishes a new one and frees a replaced one once no callback that
// could have seen it is still running (RCU, as the bench swaps its bus).
class EngineHost
{
public:
    EngineHost (a3::EngineConfig, a3::SpatialConfig, const EngineTruth&,
                std::function<void (const juce::OSCMessage&)> sendToCore = {});
    ~EngineHost();   // the backend has stopped calling audioCallback

    // Builds and prepares a new Engine (allocates; control thread). fadeFirst = false:
    // it takes over at once with the old one's gate levels (a quantum change: no re-fade).
    // fadeFirst = true: the running Engine closes (10 ms), then the new one takes over
    // closed and opens over 1 s if Core has opened. It swaps only while the audio callback is
    // running (the old Engine must be heard to go silent), so a stopped or closed backend never
    // swaps: the app's Save of N, layout or backend does not use this, it re-creates the whole
    // Session (new EngineHost, new boot id).
    juce::Result rebuild (double sampleRate, int blockSize, bool fadeFirst);

    // Control thread: closes the gates (10 ms fade) and waits, calling `pump` between looks (the app
    // sleeps a few ms; a test drives callbacks), up to maxPumps times. True once every zone is at
    // exactly 0 or when no Engine runs. A Save does this before it drops the Session: no hard cut.
    bool closeGate (const std::function<void()>& pump, int maxPumps);

    // Audio thread. Silence on every output while no Engine is published.
    void audioCallback (const a3::EngineInputs&, const a3::EngineOutputs&, int numSamples);

    // Control thread, ~50 Hz: a waiting rebuild takes over once the old Engine is silent,
    // replaced Engines are freed, the delay's modulation and the hello run, the meters are taken.
    void controlTick (double nowSeconds);

    a3::EngineControls& controls();
    juce::String bootId() const;
    void setDeckAudio (const std::vector<bool>& linked);   // control thread; see HelloSender
    const a3::EngineConfig& engineConfig() const;     // what every rebuilt Engine is built from
    const a3::SpatialConfig& spatialConfig() const;   // (the backend names its ports after them)

    // Control thread: observation (status page, tests).
    std::vector<a3::Engine::OutputMeter> meters() const;   // as of the last controlTick
    a3::GateLevels gateLevels() const;                      // all 0 while no Engine runs
    int preparedBlockSize() const;                          // 0 while no Engine runs
    a3::Engine* engine();                                   // the published Engine, or nullptr

private:
    void publish (std::unique_ptr<a3::Engine>);
    void freeWhatNoCallbackSees();

    a3::EngineConfig config;
    a3::SpatialConfig spatial;
    a3::EngineControls wire;
    HelloSender hello;

    std::atomic<a3::Engine*> current { nullptr };
    std::unique_ptr<a3::Engine> waiting;   // rebuilt with fadeFirst, until the current one is silent
    struct Retired { std::unique_ptr<a3::Engine> engine; std::uint64_t callbacksBegun; };
    std::vector<Retired> retired;
    std::atomic<std::uint64_t> begun { 0 }, ended { 0 };   // audio callbacks

    std::vector<a3::Engine::OutputMeter> lastMeters;
};
}
