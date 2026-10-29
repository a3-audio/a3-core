// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemoteHostView.h"

#include <algorithm>
#include <chrono>

#include "LoopbackHttp.h"

namespace a3app
{
static_assert (EngineControlApi::applyWaitSeconds * 1000 < RemoteHostView::applyTimeoutMillis,
               "the engine answers (even with a refusal to wait) before the window gives up");

namespace
{
juce::String problemOf (const std::string& body)
{
    const auto reply = parsedJson (body);
    return reply["problem"].isString() ? reply["problem"].toString() : juce::String ("the engine refused");
}
}

RemoteHostView::RemoteHostView (std::string hostToUse, int portToUse, bool startPolling)
    : host (std::move (hostToUse)), port (portToUse)
{
    cache.problem = "no engine at " + url();
    if (startPolling)
        poller = std::thread ([this] { run(); });
}

RemoteHostView::~RemoteHostView()
{
    stopping = true;
    wake.notify_all();
    if (poller.joinable())
        poller.join();
}

bool RemoteHostView::answers() const
{
    const auto reply = httpExchange (host, port, "GET", "/api/engine/status", {}, pollTimeoutMillis);
    return reply && reply->status == 200;
}

juce::String RemoteHostView::url() const { return "http://" + juce::String (host) + ":" + juce::String (port); }

void RemoteHostView::run()
{
    using clock = std::chrono::steady_clock;
    auto nextConfig = clock::now();
    while (! stopping)
    {
        {
            const std::lock_guard<std::mutex> round (pollLock);
            flushPosts();
            pollStatusAndMeters();
            if (clock::now() >= nextConfig)
            {
                pollConfigAndLinks();
                nextConfig = clock::now() + std::chrono::milliseconds ((int) (EngineControlApi::configPeriod * 1000));
            }
        }
        std::unique_lock<std::mutex> sleep (wakeLock);
        wake.wait_for (sleep, std::chrono::milliseconds ((int) (EngineControlApi::statusPeriod * 1000)), [this] { return stopping.load(); });
    }
}

void RemoteHostView::refreshNow()
{
    const std::lock_guard<std::mutex> round (pollLock);
    flushPosts();
    pollStatusAndMeters();
    pollConfigAndLinks();
}

void RemoteHostView::forgetConfigBody()
{
    const std::lock_guard<std::mutex> guard (cacheLock);
    cache.configBody.clear();
}

void RemoteHostView::noEngine()
{
    const std::lock_guard<std::mutex> guard (cacheLock);
    cache.report = {};   // as the local view says when nothing runs
    cache.meters.meters.clear();
    cache.problem = "no engine at " + url();
}

void RemoteHostView::pollStatusAndMeters()
{
    const auto status = httpExchange (host, port, "GET", "/api/engine/status", {}, pollTimeoutMillis);
    const auto meters = httpExchange (host, port, "GET", "/api/engine/meters", {}, pollTimeoutMillis);
    const auto statusReport = status && status->status == 200 ? statusFromJson (parsedJson (status->body)) : std::nullopt;
    const auto metersReport = meters && meters->status == 200 ? metersFromJson (parsedJson (meters->body)) : std::nullopt;
    if (! statusReport || ! metersReport)
        return noEngine();
    const std::lock_guard<std::mutex> guard (cacheLock);
    cache.report = *statusReport;
    cache.meters = *metersReport;
    cache.problem = statusReport->engineProblem;
}

void RemoteHostView::pollConfigAndLinks()
{
    const auto config = httpExchange (host, port, "GET", "/api/engine/config", {}, pollTimeoutMillis);
    const auto links = httpExchange (host, port, "GET", "/api/engine/links", {}, pollTimeoutMillis);
    if (! config || config->status != 200 || ! links || links->status != 200)
        return;   // pollStatusAndMeters says that no engine answers
    const auto linksReport = linksFromJson (parsedJson (links->body));

    // postLock first, as edit() takes it: an edit of ours that is on its way must not be overwritten by the
    // engine's older pending config.
    const std::lock_guard<std::mutex> posting (postLock);
    const std::lock_guard<std::mutex> guard (cacheLock);
    if (linksReport)
        cache.links = *linksReport;
    if (! posts.empty() || config->body == cache.configBody)
        return;
    const auto body = parsedJson (config->body);
    const juce::File file (body["file"].toString());
    juce::String error;
    const int truthDecks = body["truthDecks"];
    const auto pending = parseAppConfig (body["pending"], file.getParentDirectory(), error, truthDecks);
    const auto running = parseAppConfig (body["running"], file.getParentDirectory(), error, truthDecks);
    if (! pending || ! running)
        return;
    cache.pending = *pending;
    cache.running = *running;
    cache.file = file;
    cache.truthDecks = truthDecks;
    cache.configBody = config->body;
}

void RemoteHostView::flushPosts()
{
    for (;;)
    {
        Post post;
        {
            const std::lock_guard<std::mutex> guard (postLock);
            if (posts.empty())
                return;
            post = posts.front();
        }
        const auto answer = httpExchange (host, port, "POST", post.path, post.body, pollTimeoutMillis);
        const bool sent = answer && ! answer->timedOut;
        if (sent && answer->status != 200)
            forgetConfigBody();   // refused: the next poll shows what the engine really has pending
        {
            const std::lock_guard<std::mutex> guard (postLock);
            if (! sent)
                return;   // the engine is not there; the post stays for the next round
            if (! posts.empty() && posts.front().body == post.body && posts.front().path == post.path)
                posts.pop_front();
        }
    }
}

BackendStatus RemoteHostView::status() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.report.backend; }
std::vector<a3::Engine::OutputMeter> RemoteHostView::meters() { const std::lock_guard<std::mutex> g (cacheLock); return cache.meters.meters; }
std::vector<std::string> RemoteHostView::outputNames() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.meters.outputNames; }
bool RemoteHostView::coreHeard() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.report.coreHeard; }
double RemoteHostView::secondsSinceCore() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.report.secondsSinceCore; }
bool RemoteHostView::gateOpen() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.report.gateOpen; }
a3::GateLevels RemoteHostView::gateLevels() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.report.gateLevels; }
AppConfig RemoteHostView::config() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.pending; }
AppConfig RemoteHostView::running() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.running; }
juce::String RemoteHostView::engineProblem() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.problem; }
std::vector<std::string> RemoteHostView::droppedLinks() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.report.droppedLinks; }
std::vector<std::string> RemoteHostView::ignoredKeys() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.report.ignoredKeys; }
juce::String RemoteHostView::truthNote() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.report.truthNote; }
int RemoteHostView::truthDecks() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.truthDecks; }
juce::File RemoteHostView::configFile() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.file; }
LiveLinks RemoteHostView::liveLinks() const { const std::lock_guard<std::mutex> g (cacheLock); return cache.links; }

void RemoteHostView::dropQueuedEdits()   // postLock held
{
    posts.erase (std::remove_if (posts.begin(), posts.end(), [] (const Post& queued) { return queued.isEdit; }), posts.end());
}

void RemoteHostView::edit (const AppConfig& config)
{
    auto body = juce::JSON::toString (toJson (config), true).toStdString();
    {
        const std::lock_guard<std::mutex> posting (postLock);
        {
            const std::lock_guard<std::mutex> guard (cacheLock);
            cache.pending = config;
        }
        dropQueuedEdits();   // only the last edit matters
        posts.push_back ({ "/api/engine/edit", std::move (body), true });
    }
    wake.notify_all();
}

void RemoteHostView::setFilterKind (a3::FilterKind kind)
{
    const juce::String name = kind == a3::FilterKind::svf ? "svf" : "ladder";
    {
        const std::lock_guard<std::mutex> posting (postLock);
        {
            const std::lock_guard<std::mutex> guard (cacheLock);   // it runs now, as the local view says
            cache.pending.engine.filterKind = cache.running.engine.filterKind = kind;
        }
        posts.push_back ({ "/api/engine/filter-kind", "{\"kind\":\"" + name.toStdString() + "\"}", false });
    }
    wake.notify_all();
}

void RemoteHostView::playNoise (const NoiseRequest& request)
{
    {
        const std::lock_guard<std::mutex> posting (postLock);
        posts.push_back ({ "/api/engine/noise", juce::JSON::toString (toJson (request), true).toStdString(), false });
    }
    wake.notify_all();
}

juce::Result RemoteHostView::apply (const AppConfig& config)
{
    const std::lock_guard<std::mutex> round (pollLock);
    {
        const std::lock_guard<std::mutex> guard (postLock);
        dropQueuedEdits();   // the apply carries the config
    }
    const auto answer = httpExchange (host, port, "POST", "/api/engine/apply", juce::JSON::toString (toJson (config), true).toStdString(),
                                  applyTimeout);
    if (! answer)
        return juce::Result::fail ("no engine at " + url());
    if (answer->timedOut)
        return juce::Result::fail ("the engine at " + url() + " did not answer in " + juce::String (applyTimeout / 1000.0, 1) + " s; it may still be applying");
    if (answer->status != 200)
        return juce::Result::fail (problemOf (answer->body));
    const std::lock_guard<std::mutex> guard (cacheLock);
    cache.pending = cache.running = config;
    cache.configBody.clear();
    return juce::Result::ok();
}
}
