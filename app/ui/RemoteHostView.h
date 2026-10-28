// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window's HostView over the engine's control API (EngineControlApi.h). A background thread polls
// status and meters at 20 Hz and config and links at 2 Hz into a cache; every getter reads that cache and
// never the network, so a page that paints on the message thread cannot wait for an engine. Edits and the
// filter kind are posted by the same thread, in order; apply posts at once and waits for the answer.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "EngineControlApi.h"

namespace a3app
{
class RemoteHostView final : public HostView
{
public:
    static constexpr int pollTimeoutMillis = 500;
    static constexpr int applyTimeoutMillis = 15000;   // longer than EngineControlApi::applyWaitSeconds

    // `startPolling` false: nothing polls until refreshNow() is called (the tests drive it).
    RemoteHostView (std::string host, int port, bool startPolling = true);
    ~RemoteHostView() override;

    // One round on the caller's thread: the queued posts, then every poll. The window calls it once before
    // its first paint.
    void refreshNow();
    juce::String url() const;
    // Whether an engine answers at all, asked now (GET /api/engine/status).
    bool answers() const;
    void setApplyTimeoutMillis (int millis) { applyTimeout = millis; }   // the tests'

    BackendStatus status() const override;
    std::vector<a3::Engine::OutputMeter> meters() override;
    std::vector<std::string> outputNames() const override;
    bool coreHeard() const override;
    double secondsSinceCore() const override;
    bool gateOpen() const override;
    a3::GateLevels gateLevels() const override;
    AppConfig config() const override;
    AppConfig running() const override;
    juce::String engineProblem() const override;
    std::vector<std::string> droppedLinks() const override;
    std::vector<std::string> ignoredKeys() const override;
    juce::String truthNote() const override;
    juce::File configFile() const override;
    LiveLinks liveLinks() const override;
    void edit (const AppConfig&) override;
    juce::Result apply (const AppConfig&) override;
    void setFilterKind (a3::FilterKind) override;

private:
    struct Post
    {
        std::string path, body;
        bool isEdit = false;
    };
    struct Cache
    {
        EngineStatusReport report;
        EngineMetersReport meters;
        AppConfig pending, running;
        juce::File file;
        LiveLinks links;
        juce::String problem;
        std::string configBody;   // the last config answer's bytes: parsed again only when they change
    };

    void run();
    void flushPosts();
    void pollStatusAndMeters();
    void pollConfigAndLinks();
    void noEngine();
    void forgetConfigBody();
    void dropQueuedEdits();

    const std::string host;
    const int port;
    std::mutex pollLock;   // one round at a time: the thread's, refreshNow's and apply's
    mutable std::mutex cacheLock;
    Cache cache;
    std::mutex postLock;
    std::deque<Post> posts;
    std::atomic<int> applyTimeout { applyTimeoutMillis };
    std::atomic<bool> stopping { false };
    std::mutex wakeLock;
    std::condition_variable wake;
    std::thread poller;
};
}
