// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "JackBackend.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace a3app
{
namespace
{
constexpr const char* clientName = "a3-engine";

// jack_get_ports / jack_port_get_all_connections hand out a null-terminated list the caller frees.
std::vector<std::string> takeNames (const char** names)
{
    std::vector<std::string> list;
    if (names == nullptr)
        return list;
    for (auto** name = names; *name != nullptr; ++name)
        list.emplace_back (*name);
    jack_free (names);
    return list;
}
}

JackBackend::JackBackend (bool shouldConnect, std::string hostName) : connecting (shouldConnect), hostname (std::move (hostName)) {}

JackBackend::~JackBackend() { close(); }

juce::Result JackBackend::open (EngineHost& engineHost, const AppConfig& config)
{
    if (client != nullptr)
        return juce::Result::fail ("the JACK backend is already open");

    // The name is fixed: the patchbay and Core know the engine by it (spec D9).
    jack_status_t status {};
    client = jack_client_open (clientName, (jack_options_t) (JackNoStartServer | JackUseExactName), &status);
    if (client == nullptr)
        return juce::Result::fail ((status & JackNameNotUnique) != 0 ? "a JACK client named a3-engine is already running"
                                                                     : "no JACK server running");
    const auto names = enginePortNames (engineHost.engineConfig(), engineHost.spatialConfig());
    linker = std::make_unique<a3::links::JackLinker> (client, clientName, names.inputs);
    const auto refused = refuseBesideReaper (a3::links::clientNamesOf (linker->graphPorts()), connecting);
    if (refused.failed())
    {
        close();
        return refused;
    }
    if (! registerPorts (names))
    {
        close();
        return juce::Result::fail ("cannot register the JACK ports");
    }
    slots = bindPortSlots (engineHost.engineConfig(), engineHost.spatialConfig(), inputs, outputs);
    map = linkMapFor (config, hostname);
    sampleRate = (double) jack_get_sample_rate (client);
    const auto built = engineHost.rebuild (sampleRate, (int) jack_get_buffer_size (client), false);
    if (built.failed())
    {
        close();
        return built;
    }
    host = &engineHost;

    jack_set_process_callback (client, processCallback, this);
    jack_set_buffer_size_callback (client, bufferSizeCallback, this);
    jack_set_sample_rate_callback (client, sampleRateCallback, this);
    jack_set_port_registration_callback (client, registrationCallback, this);
    jack_set_xrun_callback (client, xrunCallback, this);
    jack_on_shutdown (client, shutdownCallback, this);
    if (jack_activate (client) != 0)
    {
        close();
        return juce::Result::fail ("cannot activate the JACK client");
    }
    if (connecting)
    {
        linker->apply (map);
        linksPlanned = true;
    }
    return juce::Result::ok();
}

void JackBackend::close()
{
    if (client == nullptr)
        return;
    if (! serverGone.load())
    {
        linker->removeOwned();   // the direct links would outlive the engine's ports otherwise
        jack_deactivate (client);
    }
    linker.reset();
    linksPlanned = false;
    jack_client_close (client);
    client = nullptr;
    host = nullptr;
    inputPorts.clear();
    outputPorts.clear();
    slots = {};
}

namespace
{
// The largest latency JACK reports for `mode` over `ports` (the max of each range): the path through the
// longest of the engine's links to a converter. A port with no link reports 0.
int largestLatency (const std::vector<jack_port_t*>& ports, jack_latency_callback_mode_t mode)
{
    jack_nframes_t largest = 0;
    for (auto* port : ports)
    {
        jack_latency_range_t range { 0, 0 };
        jack_port_get_latency_range (port, mode, &range);
        largest = std::max (largest, range.max);
    }
    return (int) largest;
}
}

BackendStatus JackBackend::status() const
{
    BackendStatus s;
    s.rateSource = "PipeWire (pw-metadata -n settings; a3-core's pipewire config)";
    s.bitDepth = 32;
    s.connecting = connecting;
    s.linksPlanned = linksPlanned;
    s.xruns = xruns.load();
    if (linker != nullptr)
        for (const auto& peer : linker->missingPeers())
            s.missingPeers.push_back (juce::String (peer));
    if (client == nullptr || serverGone.load())
        return s;

    s.sampleRate = (double) jack_get_sample_rate (client);
    s.blockSize = (int) jack_get_buffer_size (client);
    s.captureLatency = largestLatency (inputPorts, JackCaptureLatency);
    s.playbackLatency = largestLatency (outputPorts, JackPlaybackLatency);
    // jack_cpu_load is the whole graph's load and already holds the engine's time; the engine's
    // own block time against the period shows when the engine alone is the heavy part.
    s.dspLoadPercent = combinedLoadPercent (jack_cpu_load (client), engineBlockMs.load(), (int) lastBlockFrames.load(), s.sampleRate);
    return s;
}

std::vector<std::pair<std::string, std::string>> JackBackend::liveLinks() const
{
    std::vector<std::pair<std::string, std::string>> links;
    if (client == nullptr || serverGone.load())
        return links;
    for (auto* port : inputPorts)
        for (const auto& peer : takeNames (jack_port_get_all_connections (client, port)))
            links.emplace_back (peer, jack_port_name (port));
    for (auto* port : outputPorts)
        for (const auto& peer : takeNames (jack_port_get_all_connections (client, port)))
            links.emplace_back (jack_port_name (port), peer);
    return links;
}

void JackBackend::service (double nowSeconds)
{
    if (client == nullptr || serverGone.load())
        return;
    rebuildOnNewQuantumOrRate();
    const bool portsCameOrWent = portsChanged.exchange (false);
    if (! linkPlanDue (connecting, portsCameOrWent, nowSeconds, lastConnectSeconds))
        return;
    lastConnectSeconds = nowSeconds;
    linker->apply (map);
    linksPlanned = true;
}

bool JackBackend::lost() const { return serverGone.load(); }

// A new quantum takes over at once (no re-fade); the engine chunks a longer call meanwhile.
void JackBackend::rebuildOnNewQuantumOrRate()
{
    const int rate = pendingSampleRate.exchange (0);
    const int quantum = pendingBlockSize.exchange (0);
    const bool newRate = rate > 0 && (double) rate != sampleRate;
    const bool newQuantum = quantum > 0 && quantum != host->preparedBlockSize();
    if (! newRate && ! newQuantum)
        return;
    if (newRate)
        sampleRate = (double) rate;
    const auto built = host->rebuild (sampleRate, (int) jack_get_buffer_size (client), false);
    if (built.failed())
        std::fprintf (stderr, "a3-engine: cannot rebuild for %.0f Hz / %d frames: %s\n", sampleRate,
                      (int) jack_get_buffer_size (client), built.getErrorMessage().toRawUTF8());
}

int JackBackend::processCallback (jack_nframes_t frames, void* self)
{
    static_cast<JackBackend*> (self)->process (frames);
    return 0;
}

void JackBackend::process (jack_nframes_t frames)
{
    juce::ScopedNoDenormals noDenormals;
    const auto started = std::chrono::steady_clock::now();
    for (size_t i = 0; i < inputPorts.size(); ++i)
        *slots.inputs[i] = static_cast<const float*> (jack_port_get_buffer (inputPorts[i], frames));
    for (size_t i = 0; i < outputPorts.size(); ++i)
        *slots.outputs[i] = static_cast<float*> (jack_port_get_buffer (outputPorts[i], frames));
    host->audioCallback (inputs, outputs, (int) frames);
    engineBlockMs.store ((float) std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count(),
                         std::memory_order_relaxed);
    lastBlockFrames.store (frames, std::memory_order_relaxed);
}

int JackBackend::bufferSizeCallback (jack_nframes_t frames, void* self)
{
    static_cast<JackBackend*> (self)->pendingBlockSize.store ((int) frames);
    return 0;
}

int JackBackend::sampleRateCallback (jack_nframes_t rate, void* self)
{
    static_cast<JackBackend*> (self)->pendingSampleRate.store ((int) rate);
    return 0;
}

void JackBackend::registrationCallback (jack_port_id_t, int, void* self)
{
    static_cast<JackBackend*> (self)->portsChanged.store (true);
}

int JackBackend::xrunCallback (void* self)
{
    static_cast<JackBackend*> (self)->xruns.fetch_add (1);
    return 0;
}

void JackBackend::shutdownCallback (void* self)
{
    static_cast<JackBackend*> (self)->serverGone.store (true);
}

bool JackBackend::registerPorts (const PortNames& names)
{
    const auto add = [this] (const std::string& name, unsigned long flags)
    {
        return jack_port_register (client, name.c_str(), JACK_DEFAULT_AUDIO_TYPE, flags, 0);
    };
    for (const auto& name : names.inputs)
        if (inputPorts.emplace_back (add (name, JackPortIsInput)) == nullptr)
            return false;
    for (const auto& name : names.outputs)
        if (outputPorts.emplace_back (add (name, JackPortIsOutput)) == nullptr)
            return false;
    return true;
}
}
