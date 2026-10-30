// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's control API (P2): what the window, a separate process, sees of the engine over HTTP on
// loopback (the truth's engine.control). The routes serve snapshots that the engine's control tick
// publishes; an edit, a filter kind, a noise burst and an apply are queued to that same tick (P8: the HTTP threads
// never touch the host, the backend or the pending config themselves). Bodies are engine.json's
// format (ConfigFile.h's toJson).
#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "HttpServer.h"
#include "ui/HostView.h"

namespace a3app
{
// GET /api/engine/status: the backend's status and what the pages ask the host view beside it.
struct EngineStatusReport
{
    BackendStatus backend;
    bool coreHeard = false;
    double secondsSinceCore = 0.0;
    bool gateOpen = false;
    a3::GateLevels gateLevels;
    juce::String engineProblem;
    std::vector<std::string> droppedLinks;   // HostView::droppedLinks
    std::vector<std::string> ignoredKeys;    // HostView::ignoredKeys
    juce::String truthNote;                  // HostView::truthNote
    a3clock::ClockReport clock;              // HostView::clock
};
juce::var toJson (const EngineStatusReport&);
std::optional<EngineStatusReport> statusFromJson (const juce::var&);

// GET /api/engine/meters: one name per meter.
struct EngineMetersReport
{
    std::vector<std::string> outputNames;
    std::vector<a3::Engine::OutputMeter> meters;
};
juce::var toJson (const EngineMetersReport&);
std::optional<EngineMetersReport> metersFromJson (const juce::var&);

// POST /api/engine/noise: {"zone": "main"|"booth", "channel": 1..64} or {"zone", "azimuth", "elevation"}.
juce::var toJson (const NoiseRequest&);
std::optional<NoiseRequest> noiseFromJson (const juce::var&, juce::String& why);

juce::var toJson (const LiveLinks&);
std::optional<LiveLinks> linksFromJson (const juce::var&);

class EngineControlApi
{
public:
    static constexpr double statusPeriod = 0.05;   // 20 Hz: what the window polls status and meters at
    static constexpr double configPeriod = 0.5;    // 2 Hz: config and links
    // Shorter than the window's wait (RemoteHostView::applyTimeoutMillis), so the window gets this side's
    // "did not answer" and not a timeout of its own while the apply may still run.
    static constexpr double applyWaitSeconds = 12.0;

    // `local` is read and changed on the control thread only (tick).
    explicit EngineControlApi (HostView& local);
    ~EngineControlApi();

    EngineControlApi (const EngineControlApi&) = delete;
    EngineControlApi& operator= (const EngineControlApi&) = delete;

    a3core::Routes routes();

    // The control thread, at least every statusPeriod: runs what the routes queued, keeps the meters'
    // peaks, and publishes the snapshots that are due.
    void tick (double nowSeconds);

    // Handlers that wait for the control thread give up; nothing queued runs afterwards.
    void close();

    void setApplyWaitSeconds (double seconds) { applyWait = seconds; }   // the tests'

private:
    using Pending = std::shared_ptr<std::promise<juce::Result>>;
    struct Task
    {
        std::function<juce::Result()> run;
        Pending done;
    };

    a3core::HttpResponse status() const;
    a3core::HttpResponse meters();
    a3core::HttpResponse config() const;
    a3core::HttpResponse links() const;
    a3core::HttpResponse edit (const a3core::HttpRequest&);
    a3core::HttpResponse apply (const a3core::HttpRequest&);
    a3core::HttpResponse filterKind (const a3core::HttpRequest&);
    a3core::HttpResponse noise (const a3core::HttpRequest&);

    std::optional<AppConfig> configOf (const a3core::HttpRequest&, a3core::HttpResponse& refusal) const;
    std::future<juce::Result> enqueue (std::function<juce::Result()>);
    void runQueued();
    void keepPeaks();
    void publishStatus();
    void publishConfig();

    HostView& view;
    const int truthDecks;   // the view's, fixed for the process: the HTTP threads parse bodies with it

    // The snapshot: written by tick, read by the handlers.
    mutable std::mutex snapshotLock;
    bool published = false;
    std::string statusText, configText, linksText;
    juce::File file;
    std::vector<std::string> names;
    std::vector<a3::Engine::OutputMeter> held;   // the peaks since the last GET; rms as of the last tick

    std::mutex queueLock;
    std::deque<Task> queue;
    std::atomic<bool> closing { false };
    std::atomic<double> applyWait { applyWaitSeconds };
    double nextStatus = 0.0, nextConfig = 0.0;
    bool configDue = true;
};

// "host:port" of the truth's engine.control, or nullopt with the reason in `why` when the truth has no
// such listener or its host is not this machine's loopback: the API changes the engine's config.
std::optional<std::string> controlBind (const EngineTruth&, std::string& why);

// The server on that bind. Null, and a line through `journal`, when it cannot be served; the engine
// runs on without it.
std::unique_ptr<a3core::HttpServer> serveEngineControl (EngineControlApi&, const EngineTruth&, const a3core::Journal& journal);
}
