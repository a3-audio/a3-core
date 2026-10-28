// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AudioBackend.h"
#include "ConnectionPlan.h"
#include "IoMap.h"
#include "EnginePorts.h"
#include "a3-links/JackLinker.h"
#include <atomic>
#include <memory>
#include <jack/jack.h>

namespace a3app
{
// The rig's backend: a JACK client named "a3-engine" (never REAPER), with the engine's
// named ports, that makes its own links from the I/O map (spec D9).
//
// Threads: the process callback only hands the port buffers to EngineHost::audioCallback;
// the buffer-size, port-registration, xrun and shutdown callbacks only set atomics. Every
// JACK graph call (connect, disconnect, port lists) and every rebuild happens in service(),
// on the message thread.
class JackBackend final : public AudioBackend
{
public:
    // connecting false: --no-connect, the plan is never applied. hostname: Platform::hostname(),
    // which names this machine's AES67 sender (linkMapFor).
    JackBackend (bool connecting, std::string hostname);
    ~JackBackend() override;

    juce::Result open (EngineHost&, const AppConfig&) override;
    void close() override;
    BackendStatus status() const override;
    std::vector<std::pair<std::string, std::string>> liveLinks() const override;
    // A new quantum rebuilds the host (no re-fade); a port registration, and every second,
    // applies the connection plan.
    void service (double nowSeconds) override;
    // The JACK server went away (it shut the client down): the app ends and its unit restarts it.
    bool lost() const;

private:
    static int processCallback (jack_nframes_t, void*);
    static int bufferSizeCallback (jack_nframes_t, void*);
    static int sampleRateCallback (jack_nframes_t, void*);
    static void registrationCallback (jack_port_id_t, int, void*);
    static int xrunCallback (void*);
    static void shutdownCallback (void*);

    void process (jack_nframes_t);
    bool registerPorts (const PortNames&);
    void rebuildOnNewQuantumOrRate();

    const bool connecting;
    const std::string hostname;
    jack_client_t* client = nullptr;
    EngineHost* host = nullptr;
    IoMap map;
    double sampleRate = 0;

    std::vector<jack_port_t*> inputPorts, outputPorts;   // enginePortNames' order
    a3::EngineInputs inputs;
    a3::EngineOutputs outputs;
    PortSlots slots;

    std::atomic<int> pendingBlockSize { 0 };        // 0: no change waiting
    std::atomic<int> pendingSampleRate { 0 };
    std::atomic<bool> portsChanged { false };
    std::atomic<int> xruns { 0 };
    std::atomic<bool> serverGone { false };
    std::atomic<float> engineBlockMs { 0 };         // the engine's own time for the last block
    std::atomic<jack_nframes_t> lastBlockFrames { 0 };

    std::unique_ptr<a3::links::JackLinker> linker;   // exists while the client is open
    bool linksPlanned = false;
    double lastConnectSeconds = -1.0e9;
};
}
