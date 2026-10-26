// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// a3-engine: the engine app. Loads the truth, engine.json and the spatial data, builds the
// host on the chosen backend (JACK on the rig, a JUCE device elsewhere), listens for Core on
// engine.osc, says hello to core.osc and drives the host from a 50 Hz message-thread timer.
// The window comes with its own task: until then the app always runs windowless.
#include "AppArgs.h"
#include "DeviceBackend.h"
#include "EngineHost.h"
#include "JackBackend.h"
#include <csignal>
#include <cstdio>

namespace
{
volatile std::sig_atomic_t stopRequested = 0;
void onSignal (int) { stopRequested = 1; }

constexpr const char* usage =
    "usage: a3-engine [--config FILE] [--spatial DIR] [--truth FILE] [--no-connect] [--no-window]\n"
    "                 [--service] [--timing] [--check-config]\n"
    "  --config: default ~/.config/a3/engine/engine.json, else /usr/share/a3-engine/engine.json\n"
    "  --no-connect: make no JACK link (a run beside REAPER)\n"
    "  --no-window: no window (this build has none yet, so it is always on)\n"
    "  --service: the window hides on close; only SIGTERM ends the app\n"
    "  --timing: print the backend's load and xruns at exit\n"
    "  --check-config: read truth, config and spatial data, print \"ok\" or \"<file>: <reason>\", exit 0/1\n";

double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

// Core's words on engine.osc go straight into the host's controls (atomics, as the bench does).
class ControlListener final : public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    ControlListener (const a3app::StartupData& s, a3app::EngineHost& h) : data (s), host (h) {}
    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        a3app::applyEngineMessage (data.truth, host.engineConfig(), message, host.controls());
    }

private:
    const a3app::StartupData& data;
    a3app::EngineHost& host;
};

// Everything that exists only while the engine runs. A change of N (the window's Save) drops
// the session and builds a new one: new host, new boot id, new ports.
class Session
{
public:
    explicit Session (const a3app::StartupData& startup) : data (startup) {}
    ~Session() { close(); }

    juce::Result open (bool connecting)
    {
        if (! coreSender.connect (data.truth.coreHost, data.truth.corePort))
            return juce::Result::fail ("cannot send to " + data.truth.coreHost + ":" + juce::String (data.truth.corePort));
        host = std::make_unique<a3app::EngineHost> (data.config.engine, data.spatial, data.truth,
                                                    [this] (const juce::OSCMessage& m) { coreSender.send (m); });
        listener = std::make_unique<ControlListener> (data, *host);
        if (! socket.bindToPort (data.truth.enginePort, data.truth.engineBindAddress) || ! receiver.connectToSocket (socket))
            return juce::Result::fail ("cannot listen on " + data.truth.engineBindAddress + ":" + juce::String (data.truth.enginePort));
        receiver.addListener (listener.get());

        if (data.config.backend.type == a3app::BackendSettings::Type::jack)
            backend = std::make_unique<a3app::JackBackend> (connecting);
        else
            backend = std::make_unique<a3app::DeviceBackend>();
        return backend->open (*host, data.config);
    }

    void close()
    {
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
        const auto now = nowSeconds();
        host->controlTick (now);
        backend->service (now);
    }

    bool lost() const
    {
        const auto* jack = dynamic_cast<const a3app::JackBackend*> (backend.get());
        return jack != nullptr && jack->lost();
    }

    a3app::BackendStatus status() const { return backend->status(); }

private:
    const a3app::StartupData& data;
    juce::OSCSender coreSender;
    juce::DatagramSocket socket { false };
    juce::OSCReceiver receiver;
    std::unique_ptr<a3app::EngineHost> host;
    std::unique_ptr<ControlListener> listener;
    std::unique_ptr<a3app::AudioBackend> backend;
};

class EngineApp final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "a3-engine"; }
    const juce::String getApplicationVersion() override { return "0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        juce::String error;
        const auto parsed = a3app::parseAppArgs (getCommandLineParameterArray(), error);
        if (! parsed)
            return finish (2, error + "\n" + usage);
        args = *parsed;

        if (args.checkConfig)
        {
            juce::String message;
            const bool good = a3app::checkConfig (args, home(), message);
            std::printf ("%s\n", message.toRawUTF8());
            return finish (good ? 0 : 1, {});
        }

        auto loaded = a3app::loadStartupData (args, home(), error);
        if (! loaded)
            return finish (1, error);
        startup = std::move (*loaded);

        std::signal (SIGINT, onSignal);
        std::signal (SIGTERM, onSignal);
        session = std::make_unique<Session> (startup);
        const auto opened = session->open (! args.noConnect);
        if (opened.failed())
            return finish (1, opened.getErrorMessage());

        const auto status = session->status();
        std::fprintf (stderr, "a3-engine: %s, %.0f Hz, %d frames, %s, no window\n", startup.configFile.getFullPathName().toRawUTF8(),
                      status.sampleRate, status.blockSize, status.connecting ? "connecting" : "no links");
        startTimerHz (50);
    }

    void shutdown() override
    {
        stopTimer();
        if (session != nullptr && args.timing)
        {
            const auto status = session->status();
            std::fprintf (stderr, "a3-engine: dsp load %.1f %%, %d xruns\n", status.dspLoadPercent, status.xruns);
        }
        session.reset();
    }

    void systemRequestedQuit() override { quit(); }   // with a window, --service hides it instead

    void anotherInstanceStarted (const juce::String&) override {}

private:
    static juce::File home() { return juce::File::getSpecialLocation (juce::File::userHomeDirectory); }

    void finish (int code, const juce::String& message)
    {
        if (message.isNotEmpty())
            std::fprintf (stderr, "a3-engine: %s\n", message.toRawUTF8());
        setApplicationReturnValue (code);
        quit();
    }

    void timerCallback() override
    {
        session->tick();
        if (stopRequested == 0 && ! session->lost())
            return;
        stopTimer();
        finish (session->lost() ? 1 : 0, session->lost() ? "the JACK server went away" : juce::String());
    }

    a3app::AppArgs args;
    a3app::StartupData startup;
    std::unique_ptr<Session> session;
};
}

START_JUCE_APPLICATION (EngineApp)
