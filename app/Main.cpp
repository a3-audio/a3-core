// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// a3-engine: the engine app. Loads the truth, engine.json and the spatial data, builds the
// host on the chosen backend (JACK on the rig, a JUCE device elsewhere), listens for Core on
// engine.osc, says hello to core.osc and drives the host from a 50 Hz message-thread timer.
// Without --no-window it opens the audio and status pages; --service hides the window on close.
#include "AppArgs.h"
#include "DeviceBackend.h"
#include "EngineHost.h"
#include "JackBackend.h"
#include "ui/HostView.h"
#include "ui/MainWindow.h"
#include <csignal>
#include <cstdio>
#include <functional>

namespace
{
volatile std::sig_atomic_t stopRequested = 0;
void onSignal (int) { stopRequested = 1; }

constexpr const char* usage =
    "usage: a3-engine [--config FILE] [--spatial DIR] [--truth FILE] [--no-connect] [--no-window]\n"
    "                 [--service] [--timing] [--check-config]\n"
    "  --config: default ~/.config/a3/engine/engine.json, else /usr/share/a3-engine/engine.json\n"
    "  --no-connect: make no JACK link (a run beside REAPER)\n"
    "  --no-window: no window (a unit without a screen)\n"
    "  --service: the window hides on close; only SIGTERM ends the app\n"
    "  --timing: print the backend's load and xruns at exit\n"
    "  --check-config: read truth, config and spatial data, print \"ok\" or \"<file>: <reason>\", exit 0/1\n";

double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

// Core's words on engine.osc go straight into the host's controls (atomics, as the bench does).
class ControlListener final : public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    ControlListener (const a3app::StartupData& s, a3app::EngineHost& h, std::atomic<double>& lastCore)
        : data (s), host (h), lastMessage (lastCore) {}
    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        lastMessage.store (nowSeconds());
        a3app::applyEngineMessage (data.truth, host.engineConfig(), message, host.controls());
    }

private:
    const a3app::StartupData& data;
    a3app::EngineHost& host;
    std::atomic<double>& lastMessage;
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
        listener = std::make_unique<ControlListener> (data, *host, lastCoreMessage);
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
    a3app::LiveLinks liveLinks() const { return backend->liveLinks(); }
    a3app::EngineHost& engineHost() { return *host; }
    // Seconds on the app clock of Core's last message, or a negative number before the first.
    double lastCoreSeconds() const { return lastCoreMessage.load(); }

private:
    std::atomic<double> lastCoreMessage { -1.0 };
    const a3app::StartupData& data;
    juce::OSCSender coreSender;
    juce::DatagramSocket socket { false };
    juce::OSCReceiver receiver;
    std::unique_ptr<a3app::EngineHost> host;
    std::unique_ptr<ControlListener> listener;
    std::unique_ptr<a3app::AudioBackend> backend;
};

// What the pages see of the running Session. An edit is kept here until Save; apply() hands it to
// the app, which re-creates the Session (see EngineApp::restart).
class SessionView final : public a3app::HostView
{
public:
    using Restart = std::function<juce::Result (const a3app::AppConfig&)>;
    SessionView (std::unique_ptr<Session>& s, const a3app::StartupData& startup, Restart restartApp)
        : session (s), file (startup.configFile), edited (startup.config), startedWith (startup.config),
          names (portNames (startup)), restart (std::move (restartApp)) {}

    a3app::BackendStatus status() const override { return session->status(); }
    std::vector<a3::Engine::OutputMeter> meters() override { return session->engineHost().meters(); }
    std::vector<std::string> outputNames() const override { return names; }
    bool coreHeard() const override { return session->lastCoreSeconds() >= 0.0; }
    double secondsSinceCore() const override { return nowSeconds() - session->lastCoreSeconds(); }
    bool gateOpen() const override { return session->engineHost().controls().global().gateOpen.load(); }
    a3::GateLevels gateLevels() const override { return session->engineHost().gateLevels(); }
    a3app::AppConfig config() const override { return edited; }
    a3app::AppConfig running() const override { return startedWith; }
    juce::File configFile() const override { return file; }
    a3app::LiveLinks liveLinks() const override { return session->liveLinks(); }
    void edit (const a3app::AppConfig& config) override { edited = config; }

    juce::Result apply (const a3app::AppConfig& config) override
    {
        const auto result = restart (config);
        if (result.failed())
            return result;
        startedWith = edited = config;
        names = portNames (config);
        return juce::Result::ok();
    }

    void setFilterKind (a3::FilterKind kind) override
    {
        session->engineHost().controls().global().filter.kind.store (static_cast<int> (kind));
        edited.engine.filterKind = startedWith.engine.filterKind = kind;   // it runs now
    }

private:
    static std::vector<std::string> portNames (const a3app::StartupData& startup) { return portNames (startup.config, startup.spatial); }
    static std::vector<std::string> portNames (const a3app::AppConfig& config, const a3::SpatialConfig& spatial)
    {
        return a3app::enginePortNames (config.engine, spatial).outputs;
    }
    std::vector<std::string> portNames (const a3app::AppConfig& config) const
    {
        juce::String error;
        const auto ports = a3app::portNamesFor (config, error);
        return ports ? ports->outputs : names;
    }

    std::unique_ptr<Session>& session;
    const juce::File file;
    a3app::AppConfig edited, startedWith;
    std::vector<std::string> names;
    Restart restart;
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
        std::fprintf (stderr, "a3-engine: %s, %.0f Hz, %d frames, %s%s\n", startup.configFile.getFullPathName().toRawUTF8(),
                      status.sampleRate, status.blockSize, status.connecting ? "connecting" : "no links", args.noWindow ? ", no window" : "");
        if (! args.noWindow)
        {
            view = std::make_unique<SessionView> (session, startup, [this] (const a3app::AppConfig& config) { return restart (config); });
            window = std::make_unique<a3app::MainWindow> (*view, pageDevices, spatialChoices(), args.service, [this] { quit(); });
            window->open();
        }
        startTimerHz (50);
    }

    void shutdown() override
    {
        stopTimer();
        window.reset();   // before the session its pages read
        view.reset();
        if (session != nullptr && args.timing)
        {
            const auto status = session->status();
            std::fprintf (stderr, "a3-engine: dsp load %.1f %%, %d xruns\n", status.dspLoadPercent, status.xruns);
        }
        session.reset();
    }

    void systemRequestedQuit() override { quit(); }   // SIGTERM and a desktop's logout; the service window's close button only hides

    void anotherInstanceStarted (const juce::String&) override {}

private:
    static juce::File home() { return juce::File::getSpecialLocation (juce::File::userHomeDirectory); }

    // Save of a changed config: the whole Session is dropped and built again (new host, new boot
    // id, new ports; Core replays through the hello). That covers N, layout and backend changes
    // and a backend that is stopped, which a fade-first rebuild could not swap. The old Session
    // closes first because both bind engine.osc and may hold the device. If the new one does not
    // open, the old config is opened again.
    juce::Result restart (const a3app::AppConfig& config)
    {
        juce::String error;
        const auto spatial = a3::loadSpatialConfig (config.spatialDirectory, error);
        if (! spatial)
            return juce::Result::fail ("spatial: " + error);

        const auto before = startup;
        session.reset();
        startup.config = config;
        startup.spatial = *spatial;
        session = std::make_unique<Session> (startup);
        const auto opened = session->open (! args.noConnect);
        if (opened.wasOk())
            return opened;

        session.reset();
        startup = before;
        session = std::make_unique<Session> (startup);
        if (session->open (! args.noConnect).failed())
        {
            finish (1, "cannot reopen the previous setting after: " + opened.getErrorMessage());
            return juce::Result::fail (opened.getErrorMessage() + " (the previous setting could not be reopened either)");
        }
        return juce::Result::fail (opened.getErrorMessage() + " (the previous setting is running again)");
    }

    // The directories the ZONES page offers: the user's own and the installed variants, where they exist.
    std::vector<juce::File> spatialChoices() const
    {
        std::vector<juce::File> choices;
        for (const auto& directory : { home().getChildFile (".config/a3/engine/spatial"), juce::File ("/usr/share/a3-engine/spatial"),
                                       juce::File ("/usr/share/a3-engine/spatial-reaper-booth-6.1") })
            if (directory.isDirectory())
                choices.push_back (directory);
        return choices;
    }

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
    juce::AudioDeviceManager pageDevices;   // only a catalogue of device types for the audio page; never initialised, so it opens no device
    std::unique_ptr<SessionView> view;
    std::unique_ptr<a3app::MainWindow> window;
};
}

START_JUCE_APPLICATION (EngineApp)
