// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineRole.h"

#include <cstdio>

#include "DeviceBackend.h"
#include "EngineHost.h"
#include "JackBackend.h"
#include "OscVocabulary.h"
#include "UdpNet.h"

namespace a3app
{
namespace
{
void sayEngine (const juce::String& line) { std::fprintf (stderr, "a3-engine: %s\n", line.toRawUTF8()); }

// Core's words on engine.osc go straight into the host's controls (atomics, as the bench does).
class ControlListener final : public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    ControlListener (const StartupData& s, EngineHost& h) : data (s), host (h) {}
    void oscMessageReceived (const juce::OSCMessage& message) override { applyEngineMessage (data.truth, host.engineConfig(), message, host.controls()); }

private:
    const StartupData& data;
    EngineHost& host;
};
}

// Everything that exists only while the engine runs.
class EngineSession
{
public:
    explicit EngineSession (const StartupData& startup) : data (startup) {}
    ~EngineSession() { close(); }

    juce::Result open (bool connecting)
    {
        if (! coreSender.connect (data.truth.coreHost, data.truth.corePort))
            return juce::Result::fail ("cannot send to " + data.truth.coreHost + ":" + juce::String (data.truth.corePort));
        host = std::make_unique<EngineHost> (data.config.engine, data.spatial, data.truth,
                                             [this] (const juce::OSCMessage& m) { coreSender.send (m); },
                                             metersSender());
        listener = std::make_unique<ControlListener> (data, *host);
        if (! socket.bindToPort (data.truth.enginePort, data.truth.engineBindAddress) || ! receiver.connectToSocket (socket))
            return juce::Result::fail ("cannot listen on " + data.truth.engineBindAddress + ":" + juce::String (data.truth.enginePort));
        receiver.addListener (listener.get());

        if (data.config.backend.type == BackendSettings::Type::jack)
            backend = std::make_unique<JackBackend> (connecting);
        else
            backend = std::make_unique<DeviceBackend>();
        const auto opened = backend->open (*host, data.config);
        ready = opened.wasOk();
        return opened;
    }

    void close()
    {
        ready = false;
        if (backend != nullptr)
            backend->close();   // no audio callback after this
        receiver.removeListener (listener.get());
        receiver.disconnect();
        backend.reset();
        listener.reset();
        host.reset();
    }

    void tick()
    {
        if (! ready)
            return;
        const auto now = monotonicSeconds();
        host->controlTick (now);
        backend->service (now);
    }

    bool lost() const
    {
        if (! ready)
            return false;
        const auto* jack = dynamic_cast<const JackBackend*> (backend.get());
        return jack != nullptr && jack->lost();
    }

    BackendStatus status() const { return backend != nullptr ? backend->status() : BackendStatus {}; }

private:
    // The /vu bundles to core.vu-relay on loopback (O13); a truth without that listener sends none. A send
    // that fails is dropped without a word, as the analyzer's non-blocking sendto dropped it: Core may be
    // down or restarting, and the next send is 40 ms away.
    std::function<void (const EngineHost::Datagram&)> metersSender()
    {
        if (data.truth.vuRelayPort <= 0 || data.truth.vuRelayHost.isEmpty())
            return {};
        const a3core::Endpoint relay { data.truth.vuRelayHost.toStdString(), data.truth.vuRelayPort };
        return [this, relay] (const EngineHost::Datagram& bundle)
        {
            try
            {
                metersSocket.sendTo (relay, bundle);
            }
            catch (const a3core::OsError&)
            {
            }
        };
    }

    bool ready = false;
    const StartupData& data;
    juce::OSCSender coreSender;
    a3core::UdpSocket metersSocket;
    juce::DatagramSocket socket { false };
    juce::OSCReceiver receiver;
    std::unique_ptr<EngineHost> host;
    std::unique_ptr<ControlListener> listener;
    std::unique_ptr<AudioBackend> backend;
};

std::variant<std::unique_ptr<EngineRole>, int> EngineRole::start (const AppArgs& args, const juce::File& home)
{
    if (args.checkConfig)
    {
        juce::String message;
        const bool good = checkConfig (args, home, message);
        std::printf ("%s\n", message.toRawUTF8());
        std::fflush (stdout);
        return good ? 0 : 1;
    }

    juce::String error;
    auto loaded = loadStartupData (args, home, error);
    if (! loaded)
    {
        sayEngine (error);
        return 1;
    }

    std::unique_ptr<EngineRole> role (new EngineRole (args, std::move (*loaded)));
    const auto opened = role->session->open (! args.noConnect);
    if (opened.failed())
    {
        sayEngine (opened.getErrorMessage());
        return 1;
    }
    const auto status = role->session->status();
    sayEngine (role->startup.configFile.getFullPathName() + ", " + juce::String (status.sampleRate, 0) + " Hz, "
               + juce::String (status.blockSize) + " frames, " + (status.connecting ? "connecting" : "no links"));
    return role;
}

EngineRole::EngineRole (const AppArgs& argsToUse, StartupData data)
    : args (argsToUse), startup (std::move (data)), session (std::make_unique<EngineSession> (startup))
{
}

EngineRole::~EngineRole()
{
    if (args.timing)
    {
        const auto status = session->status();
        sayEngine ("dsp load " + juce::String (status.dspLoadPercent, 1) + " %, " + juce::String (status.xruns) + " xruns");
    }
    session.reset();
}

std::optional<int> EngineRole::tick (bool stopRequested)
{
    session->tick();
    if (session->lost())
    {
        sayEngine ("the JACK server went away");
        return 1;
    }
    if (stopRequested)
        return 0;
    return std::nullopt;
}
}
