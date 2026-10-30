// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineRole.h"

#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "ConfigEdits.h"
#include "DeckAudioReport.h"
#include "DelayModulation.h"
#include "DeviceBackend.h"
#include "EngineControlApi.h"
#include "EngineHost.h"
#include "JackBackend.h"
#include "OscVocabulary.h"
#include "SessionRestart.h"
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
    ControlListener (const StartupData& s, EngineHost& h, std::atomic<double>& lastCore) : data (s), host (h), lastMessage (lastCore) {}
    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        lastMessage.store (monotonicSeconds());
        applyEngineMessage (data.truth, host.engineConfig(), message, host.controls());
    }

private:
    const StartupData& data;
    EngineHost& host;
    std::atomic<double>& lastMessage;
};
}

// Everything that exists only while the engine runs.
class EngineSession
{
public:
    explicit EngineSession (const StartupData& startup) : data (startup) {}
    ~EngineSession() { close(); }

    juce::Result open (bool connecting, const std::string& hostname)
    {
        if (! coreSender.connect (data.truth.coreHost, data.truth.corePort))
            return juce::Result::fail ("cannot send to " + data.truth.coreHost + ":" + juce::String (data.truth.corePort));
        host = std::make_unique<EngineHost> (data.config.engine, data.spatial, data.truth,
                                             [this] (const juce::OSCMessage& m) { coreSender.send (m); },
                                             metersSender());
        listener = std::make_unique<ControlListener> (data, *host, lastCoreMessage);
        if (! socket.bindToPort (data.truth.enginePort, data.truth.engineBindAddress) || ! receiver.connectToSocket (socket))
            return juce::Result::fail ("cannot listen on " + data.truth.engineBindAddress + ":" + juce::String (data.truth.enginePort));
        receiver.addListener (listener.get());

        if (data.config.backend.type == BackendSettings::Type::jack)
            backend = std::make_unique<JackBackend> (connecting, hostname);
        else
            backend = std::make_unique<DeviceBackend>();
        const auto opened = backend->open (*host, data.config);
        ready = opened.wasOk();
        return opened;
    }

    // Only a session that opened has a host and a backend to tick, read or ask.
    bool isReady() const { return ready; }

    // Closes the gates and waits for the 10 ms fade, so closing the backend is not a hard cut.
    void quiet()
    {
        if (ready)
            host->closeGate ([] { juce::Thread::sleep (2); }, quietPumps);
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
        // Core hears each deck slot's link state with the hello. A backend that makes no links has
        // nothing to report: Core then counts the deck by its hello alone, as for an older engine.
        reportDeckAudio (backend->status(), data.config.io, data.config.engine.decks, *host);
    }

    bool lost() const
    {
        if (! ready)
            return false;
        const auto* jack = dynamic_cast<const JackBackend*> (backend.get());
        return jack != nullptr && jack->lost();
    }

    BackendStatus status() const { return backend != nullptr ? backend->status() : BackendStatus {}; }
    LiveLinks liveLinks() const { return backend != nullptr ? backend->liveLinks() : LiveLinks {}; }
    void measureRoundTrip() { if (ready) backend->measureRoundTrip(); }
    void measureAnalogChain() { if (ready) backend->measureAnalogChain(); }
    std::optional<ConverterShare> takeNewConverterShare() { return ready ? backend->takeNewConverterShare() : std::nullopt; }
    EngineHost& engineHost() { return *host; }
    // Seconds on the app clock of Core's last message, or a negative number before the first.
    double lastCoreSeconds() const { return lastCoreMessage.load(); }

private:
    static constexpr int quietPumps = 25;   // 25 x 2 ms: far above the 10 ms fade
    std::atomic<double> lastCoreMessage { -1.0 };
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

// What the control API sees of the running session, on the control thread. An edit is kept here until
// Save; apply() hands it to the role, which re-creates the session.
class EngineView final : public HostView
{
public:
    EngineView (EngineRole& r, juce::File saveFile, AppConfig startConfig, EngineTruth engineTruth)
        : role (r), file (std::move (saveFile)), edited (startConfig), startedWith (std::move (startConfig)), truth (std::move (engineTruth)),
          names (outputsOf (edited)) {}

    BackendStatus status() const override { return live() ? role.session->status() : BackendStatus {}; }
    std::vector<a3::Engine::OutputMeter> meters() override { return live() ? role.session->engineHost().meters() : std::vector<a3::Engine::OutputMeter> {}; }
    std::vector<std::string> outputNames() const override { return names; }
    bool coreHeard() const override { return live() && role.session->lastCoreSeconds() >= 0.0; }
    double secondsSinceCore() const override { return live() ? monotonicSeconds() - role.session->lastCoreSeconds() : 0.0; }
    bool gateOpen() const override { return live() && role.session->engineHost().controls().global().gateOpen.load(); }
    a3::GateLevels gateLevels() const override { return live() ? role.session->engineHost().gateLevels() : a3::GateLevels {}; }
    AppConfig config() const override { return edited; }
    AppConfig running() const override { return startedWith; }
    juce::File configFile() const override { return file; }
    LiveLinks liveLinks() const override { return live() ? role.session->liveLinks() : LiveLinks {}; }
    juce::String engineProblem() const override { return problem; }
    std::vector<std::string> droppedLinks() const override { return startedWith.droppedLinks; }
    std::vector<std::string> ignoredKeys() const override { return startedWith.ignoredKeys; }
    std::vector<std::string> loopDropped() const override { return startedWith.loopDropped; }
    juce::String truthNote() const override
    {
        juce::StringArray notes { a3app::truthNote (truth), unmeteredDecksNote (startedWith.engine.decks, truth.vuMeters),
                                  hiddenDecksNote (startedWith.engine.decks, startedWith.engine.decksNamed, truth.decks),
                                  a3::delayOverrideNote (role.startup.spatial.fxParameters, role.startup.config.engine.delayModulation) };
        notes.removeEmptyStrings();
        return notes.joinIntoString ("; ");
    }
    int truthDecks() const override { return truth.decks; }
    a3clock::ClockReport clock() const override { return clockReader.get (monotonicSeconds(), (double) juce::Time::currentTimeMillis() / 1000.0); }
    void edit (const AppConfig& config) override { edited = config; }

    // Fails when the new config did not open (the previous one is running again, or nothing is: then
    // engineProblem() says so). Success makes the edit what runs.
    juce::Result apply (const AppConfig& config) override
    {
        const auto result = role.restart (config, problem);
        if (result.failed())
            return result;
        startedWith = edited = config;
        startedWith.droppedLinks.clear();   // the saved file no longer names them
        edited.droppedLinks.clear();
        startedWith.ignoredKeys.clear();
        edited.ignoredKeys.clear();
        startedWith.loopDropped.clear();
        edited.loopDropped.clear();
        if (auto outputs = outputsOf (config); ! outputs.empty())
            names = std::move (outputs);
        return juce::Result::ok();
    }

    void setFilterKind (a3::FilterKind kind) override
    {
        if (live())
            role.session->engineHost().controls().global().filter.kind.store (static_cast<int> (kind));
        edited.engine.filterKind = startedWith.engine.filterKind = kind;   // it runs now
    }

    void measureRoundTrip() override
    {
        if (live())
            role.session->measureRoundTrip();
    }

    void measureAnalogChain() override
    {
        if (live())
            role.session->measureAnalogChain();
    }

    void playNoise (const NoiseRequest& request) override
    {
        auto* engine = live() ? role.session->engineHost().engine() : nullptr;
        if (engine == nullptr)
            return;
        if (request.ambisonic)
            engine->playAmbisonicNoise (request.zone, request.azimuth, request.elevation);
        else
            engine->playNoise (request.zone, request.channel);
    }

private:
    bool live() const { return role.session != nullptr && role.session->isReady(); }
    static std::vector<std::string> outputsOf (const AppConfig& config)
    {
        juce::String error;
        const auto ports = portNamesFor (config, error);
        return ports ? ports->outputs : std::vector<std::string> {};
    }

    EngineRole& role;
    const juce::File file;
    AppConfig edited, startedWith;
    EngineTruth truth;
    std::vector<std::string> names;
    juce::String problem;
    // The clock role (another process) writes its status into the runtime directory; read on the control thread.
    mutable a3clock::ClockReportReader clockReader { a3clock::clockStatusFile (runtimeDirectory(), (unsigned) getuid()) };

    static std::optional<std::string> runtimeDirectory()
    {
        const char* dir = std::getenv ("XDG_RUNTIME_DIR");
        return dir != nullptr ? std::optional<std::string> (dir) : std::nullopt;
    }

    friend class EngineRole;
};

juce::Result EngineRole::restart (const AppConfig& config, juce::String& problem)
{
    juce::String error;
    const auto spatial = loadZonesSpatial (config, error);
    if (! spatial)
        return juce::Result::fail ("spatial: " + error);

    const auto before = startup;
    auto next = startup;
    next.config = config;
    next.spatial = *spatial;
    const auto openWith = [this] (const StartupData& data)
    {
        startup = data;
        session = std::make_unique<EngineSession> (startup);
        return session->open (! args.noConnect, args.hostname);
    };

    RestartSteps steps;
    steps.quietOld = [this] { if (session != nullptr) session->quiet(); };
    steps.dropOld = [this] { session.reset(); };
    steps.openNew = [&] { return openWith (next); };
    steps.dropNew = [this] { session.reset(); };
    steps.openPrevious = [&] { return openWith (before); };
    steps.pauseMs = [] (int ms) { juce::Thread::sleep (ms); };

    const auto result = runRestart (steps);
    switch (result.outcome)
    {
        case RestartOutcome::applied:
            problem = {};
            return juce::Result::ok();
        case RestartOutcome::previousRunning:
            problem = {};
            return juce::Result::fail (result.message + " (the previous setting is running again)");
        case RestartOutcome::nothingRunning:
            break;
    }
    // Keep a closed, not-ready session so the view has something to ask; tick() ignores it.
    problem = result.message;
    sayEngine ("NO ENGINE RUNNING: " + problem);
    session = std::make_unique<EngineSession> (startup);
    return juce::Result::fail (result.message);
}

// A refused or busy engine.control is said and the engine runs on: the audio never waits for its window.
void EngineRole::serveControl()
{
    view = std::make_unique<EngineView> (*this, startup.saveFile, startup.config, startup.truth);
    api = std::make_unique<EngineControlApi> (*view);
    server = serveEngineControl (*api, startup.truth, [] (const std::string& line, bool) { sayEngine (line); });
}

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
    if (const auto note = truthNote (loaded->truth); note.isNotEmpty())
        sayEngine (note);
    if (const auto note = unmeteredDecksNote (loaded->config.engine.decks, loaded->truth.vuMeters); note.isNotEmpty())
        sayEngine (note);
    if (const auto note = hiddenDecksNote (loaded->config.engine.decks, loaded->config.engine.decksNamed, loaded->truth.decks);
        note.isNotEmpty())
        sayEngine (note);
    if (const auto note = a3::delayOverrideNote (loaded->spatial.fxParameters, loaded->config.engine.delayModulation);
        note.isNotEmpty())
        sayEngine (note);
    for (const auto& line : droppedLinkLines (loaded->config))
        sayEngine (line);

    std::unique_ptr<EngineRole> role (new EngineRole (args, std::move (*loaded)));
    const auto opened = role->session->open (! args.noConnect, args.hostname);
    if (opened.failed())
    {
        sayEngine (opened.getErrorMessage());
        return 1;
    }
    role->serveControl();
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
    if (api != nullptr)
        api->close();   // a handler that waits for a tick that will not come lets go
    server.reset();
    api.reset();
    view.reset();
    session.reset();
}

// A new converter share goes into the config file at once (only io.analogLoop changes) and into what runs and
// what is pending, so the next session and the next Save keep it.
void EngineRole::storeConverterShare()
{
    const auto share = session->takeNewConverterShare();
    if (! share)
        return;
    const auto from = startup.saveFile.existsAsFile() ? startup.saveFile : startup.configFile;
    const auto stored = a3app::storeConverterShare (from, startup.saveFile, *share, startup.truth.decks);
    sayEngine ("analog chain: converters " + juce::String (share->samples) + " samples at " + juce::String (share->sampleRate, 0) + " Hz"
               + (stored.wasOk() ? ", stored in " + startup.saveFile.getFullPathName() : ", NOT stored: " + stored.getErrorMessage()));
    for (auto* config : { &startup.config, view != nullptr ? &view->startedWith : nullptr, view != nullptr ? &view->edited : nullptr })
        if (config != nullptr && config->io.analogLoop)
            config->io.analogLoop->converters = *share;
}

std::optional<int> EngineRole::tick (bool stopRequested)
{
    session->tick();
    storeConverterShare();
    api->tick (monotonicSeconds());
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
