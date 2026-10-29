// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineControlApi.h"

#include <chrono>
#include <cmath>

#include "ConfigFile.h"
#include "Net.h"

namespace a3app
{
namespace
{
using a3core::HttpRequest;
using a3core::HttpResponse;

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> members)
{
    auto* o = new juce::DynamicObject();
    for (const auto& m : members)
        o->setProperty (m.first, m.second);
    return juce::var (o);
}

HttpResponse json (int status, const juce::var& value)
{
    HttpResponse response;
    response.status = status;
    response.contentType = "application/json";
    response.body = juce::JSON::toString (value, true).toStdString();
    return response;
}

HttpResponse problem (int status, const juce::String& why) { return json (status, object ({ { "problem", why } })); }

std::optional<double> number (const juce::var& v)
{
    if (! (v.isInt() || v.isInt64() || v.isDouble() || v.isBool()))
        return std::nullopt;
    return (double) v;
}

std::optional<juce::var> parseBody (const HttpRequest& request, juce::String& why)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (juce::String::fromUTF8 (request.body.c_str(), (int) request.body.size()), parsed);
    if (result.failed())
    {
        why = "the body is not JSON: " + result.getErrorMessage();
        return std::nullopt;
    }
    return parsed;
}
}

juce::var toJson (const EngineStatusReport& report)
{
    juce::Array<juce::var> missing, levels, dropped, ignored;
    for (const auto& peer : report.backend.missingPeers)
        missing.add (peer);
    for (const auto& link : report.droppedLinks)
        dropped.add (juce::String::fromUTF8 (link.c_str()));
    for (const auto& key : report.ignoredKeys)
        ignored.add (juce::String::fromUTF8 (key.c_str()));
    for (const auto gain : report.gateLevels.gain)
        levels.add ((double) gain);
    return object ({ { "sampleRate", report.backend.sampleRate },
                     { "blockSize", report.backend.blockSize },
                     { "rateSource", report.backend.rateSource },
                     { "dspLoadPercent", (double) report.backend.dspLoadPercent },
                     { "xruns", report.backend.xruns },
                     { "bitDepth", report.backend.bitDepth },
                     { "captureLatency", report.backend.captureLatency },
                     { "playbackLatency", report.backend.playbackLatency },
                     { "missingPeers", missing },
                     { "connecting", report.backend.connecting },
                     { "coreHeard", report.coreHeard },
                     { "secondsSinceCore", report.secondsSinceCore },
                     { "gateOpen", report.gateOpen },
                     { "gateLevels", levels },
                     { "engineProblem", report.engineProblem },
                     { "droppedLinks", dropped },
                     { "ignoredKeys", ignored },
                     { "truthNote", report.truthNote } });
}

std::optional<EngineStatusReport> statusFromJson (const juce::var& v)
{
    const auto rate = number (v["sampleRate"]), block = number (v["blockSize"]), load = number (v["dspLoadPercent"]),
               xruns = number (v["xruns"]), depth = number (v["bitDepth"]), since = number (v["secondsSinceCore"]);
    if (! rate || ! block || ! load || ! xruns || ! depth || ! since || ! v["missingPeers"].isArray() || ! v["gateLevels"].isArray()
        || v["gateLevels"].size() != a3::numGatedZones || ! v["rateSource"].isString() || ! v["engineProblem"].isString())
        return std::nullopt;
    EngineStatusReport report;
    report.backend.sampleRate = *rate;
    report.backend.blockSize = (int) *block;
    report.backend.rateSource = v["rateSource"].toString();
    report.backend.dspLoadPercent = (float) *load;
    report.backend.xruns = (int) *xruns;
    report.backend.bitDepth = (int) *depth;
    report.backend.connecting = (bool) v["connecting"];
    // An engine before the round trip sends neither: 0, the round trip is then the period alone.
    report.backend.captureLatency = (int) number (v["captureLatency"]).value_or (0.0);
    report.backend.playbackLatency = (int) number (v["playbackLatency"]).value_or (0.0);
    for (int i = 0; i < v["missingPeers"].size(); ++i)
        report.backend.missingPeers.push_back (v["missingPeers"][i].toString());
    report.coreHeard = (bool) v["coreHeard"];
    report.secondsSinceCore = *since;
    report.gateOpen = (bool) v["gateOpen"];
    for (int i = 0; i < a3::numGatedZones; ++i)
        report.gateLevels.gain[i] = (float) (double) v["gateLevels"][i];
    report.engineProblem = v["engineProblem"].toString();
    // An engine older than the deck-B ports sends neither: nothing dropped, no note.
    for (int i = 0; i < v["droppedLinks"].size(); ++i)
        report.droppedLinks.push_back (v["droppedLinks"][i].toString().toStdString());
    for (int i = 0; i < v["ignoredKeys"].size(); ++i)   // an engine before P47 sends none
        report.ignoredKeys.push_back (v["ignoredKeys"][i].toString().toStdString());
    if (v["truthNote"].isString())
        report.truthNote = v["truthNote"].toString();
    return report;
}

juce::var toJson (const EngineMetersReport& report)
{
    juce::Array<juce::var> names, meters;
    for (const auto& name : report.outputNames)
        names.add (juce::String::fromUTF8 (name.c_str()));
    for (const auto& meter : report.meters)
        meters.add (object ({ { "peak", (double) meter.peak }, { "rms", (double) meter.rms } }));
    return object ({ { "outputNames", names }, { "meters", meters } });
}

std::optional<EngineMetersReport> metersFromJson (const juce::var& v)
{
    if (! v["outputNames"].isArray() || ! v["meters"].isArray())
        return std::nullopt;
    EngineMetersReport report;
    for (int i = 0; i < v["outputNames"].size(); ++i)
        report.outputNames.push_back (v["outputNames"][i].toString().toStdString());
    for (int i = 0; i < v["meters"].size(); ++i)
    {
        const auto peak = number (v["meters"][i]["peak"]), rms = number (v["meters"][i]["rms"]);
        if (! peak || ! rms)
            return std::nullopt;
        report.meters.push_back ({ (float) *peak, (float) *rms });
    }
    return report;
}

namespace
{
constexpr int highestNoiseChannel = 64;   // the decoders' widest output (swChannel's range)

// A JSON number that is finite; never a bool or a string.
std::optional<double> finiteNumber (const juce::var& v)
{
    if (! (v.isInt() || v.isInt64() || v.isDouble()) || ! std::isfinite ((double) v))
        return std::nullopt;
    return (double) v;
}
}

juce::var toJson (const NoiseRequest& request)
{
    const juce::String zone = a3::zoneName (request.zone);
    if (request.ambisonic)
        return object ({ { "zone", zone }, { "azimuth", (double) request.azimuth }, { "elevation", (double) request.elevation } });
    return object ({ { "zone", zone }, { "channel", request.channel } });
}

std::optional<NoiseRequest> noiseFromJson (const juce::var& v, juce::String& why)
{
    NoiseRequest request;
    const auto zone = v["zone"].toString();
    if (zone != "main" && zone != "booth")
    {
        why = "zone must be \"main\" or \"booth\"";
        return std::nullopt;
    }
    request.zone = zone == "main" ? a3::SpeakerZone::main : a3::SpeakerZone::booth;
    if (v.hasProperty ("channel"))
    {
        const auto channel = finiteNumber (v["channel"]);
        if (! channel)
        {
            why = "channel must be a whole number in 1.." + juce::String (highestNoiseChannel);
            return std::nullopt;
        }
        request.channel = (int) *channel;
        if (request.channel < 1 || request.channel > highestNoiseChannel || request.channel != *channel)
        {
            why = "channel must be a whole number in 1.." + juce::String (highestNoiseChannel);
            return std::nullopt;
        }
        return request;
    }
    const auto azimuth = finiteNumber (v["azimuth"]), elevation = finiteNumber (v["elevation"]);
    if (! azimuth || ! elevation)
    {
        why = "a noise burst needs a channel, or a finite azimuth and elevation";
        return std::nullopt;
    }
    request.ambisonic = true;
    request.azimuth = (float) *azimuth;
    request.elevation = (float) juce::jlimit (-90.0, 90.0, *elevation);
    return request;
}

juce::var toJson (const LiveLinks& links)
{
    juce::Array<juce::var> all;
    for (const auto& link : links)
        all.add (juce::Array<juce::var> { juce::String::fromUTF8 (link.first.c_str()), juce::String::fromUTF8 (link.second.c_str()) });
    return all;
}

std::optional<LiveLinks> linksFromJson (const juce::var& v)
{
    if (! v.isArray())
        return std::nullopt;
    LiveLinks links;
    for (int i = 0; i < v.size(); ++i)
    {
        if (! v[i].isArray() || v[i].size() != 2)
            return std::nullopt;
        links.emplace_back (v[i][0].toString().toStdString(), v[i][1].toString().toStdString());
    }
    return links;
}

EngineControlApi::EngineControlApi (HostView& local) : view (local), truthDecks (local.truthDecks()) {}

EngineControlApi::~EngineControlApi() { close(); }

a3core::Routes EngineControlApi::routes()
{
    a3core::Routes routes;
    routes.get ("/api/engine/status", [this] (const HttpRequest&) { return status(); });
    routes.get ("/api/engine/meters", [this] (const HttpRequest&) { return meters(); });
    routes.get ("/api/engine/config", [this] (const HttpRequest&) { return config(); });
    routes.get ("/api/engine/links", [this] (const HttpRequest&) { return links(); });
    routes.post ("/api/engine/edit", [this] (const HttpRequest& r) { return edit (r); });
    routes.post ("/api/engine/apply", [this] (const HttpRequest& r) { return apply (r); });
    routes.post ("/api/engine/filter-kind", [this] (const HttpRequest& r) { return filterKind (r); });
    routes.post ("/api/engine/noise", [this] (const HttpRequest& r) { return noise (r); });
    return routes;
}

void EngineControlApi::close()
{
    closing = true;
    std::deque<Task> dropped;
    {
        const std::lock_guard<std::mutex> guard (queueLock);
        dropped.swap (queue);
    }
    for (auto& task : dropped)
        task.done->set_value (juce::Result::fail ("the engine is stopping"));
}

void EngineControlApi::tick (double now)
{
    runQueued();
    keepPeaks();
    if (now >= nextStatus)
    {
        publishStatus();
        nextStatus = now + statusPeriod;
    }
    if (configDue || now >= nextConfig)
    {
        publishConfig();
        nextConfig = now + configPeriod;
        configDue = false;
    }
}

void EngineControlApi::runQueued()
{
    for (;;)
    {
        Task task;
        {
            const std::lock_guard<std::mutex> guard (queueLock);
            if (queue.empty())
                return;
            task = std::move (queue.front());
            queue.pop_front();
        }
        task.done->set_value (task.run());
        configDue = true;   // what an edit or an apply did shows at once
    }
}

void EngineControlApi::keepPeaks()
{
    const auto now = view.meters();
    const std::lock_guard<std::mutex> guard (snapshotLock);
    if (held.size() != now.size())
        held.assign (now.size(), { 0.0f, 0.0f });
    for (size_t i = 0; i < now.size(); ++i)
        held[i] = { std::max (held[i].peak, now[i].peak), now[i].rms };
}

void EngineControlApi::publishStatus()
{
    EngineStatusReport report;
    report.backend = view.status();
    report.coreHeard = view.coreHeard();
    report.secondsSinceCore = report.coreHeard ? view.secondsSinceCore() : 0.0;
    report.gateOpen = view.gateOpen();
    report.gateLevels = view.gateLevels();
    report.engineProblem = view.engineProblem();
    report.droppedLinks = view.droppedLinks();
    report.ignoredKeys = view.ignoredKeys();
    report.truthNote = view.truthNote();
    auto text = juce::JSON::toString (toJson (report), true).toStdString();
    auto outputs = view.outputNames();

    const std::lock_guard<std::mutex> guard (snapshotLock);
    statusText = std::move (text);
    names = std::move (outputs);
    published = true;
}

void EngineControlApi::publishConfig()
{
    const auto pending = view.config(), running = view.running();
    auto configJson = juce::JSON::toString (object ({ { "pending", toJson (pending) }, { "running", toJson (running) },
                                                      { "file", view.configFile().getFullPathName() },
                                                      { "truthDecks", truthDecks } }), true).toStdString();
    auto linksJson = juce::JSON::toString (toJson (view.liveLinks()), true).toStdString();
    const auto where = view.configFile();

    const std::lock_guard<std::mutex> guard (snapshotLock);
    configText = std::move (configJson);
    linksText = std::move (linksJson);
    file = where;
}

namespace
{
HttpResponse notPublished() { return problem (503, "the engine has not published its state yet"); }
HttpResponse served (const std::string& text)
{
    HttpResponse response;
    response.contentType = "application/json";
    response.body = text;
    return response;
}
}

HttpResponse EngineControlApi::status() const
{
    const std::lock_guard<std::mutex> guard (snapshotLock);
    return published ? served (statusText) : notPublished();
}

HttpResponse EngineControlApi::config() const
{
    const std::lock_guard<std::mutex> guard (snapshotLock);
    return published ? served (configText) : notPublished();
}

HttpResponse EngineControlApi::links() const
{
    const std::lock_guard<std::mutex> guard (snapshotLock);
    return published ? served (linksText) : notPublished();
}

// The peaks go out once: the next request starts from this tick's.
HttpResponse EngineControlApi::meters()
{
    EngineMetersReport report;
    {
        const std::lock_guard<std::mutex> guard (snapshotLock);
        if (! published)
            return notPublished();
        report.outputNames = names;
        report.meters = held;
        for (auto& meter : held)
            meter.peak = 0.0f;
    }
    return json (200, toJson (report));
}

std::optional<AppConfig> EngineControlApi::configOf (const HttpRequest& request, HttpResponse& refusal) const
{
    juce::File base;
    {
        const std::lock_guard<std::mutex> guard (snapshotLock);
        base = file.getParentDirectory();
    }
    juce::String why;
    const auto body = parseBody (request, why);
    if (body)
        if (const auto parsed = parseAppConfig (*body, base, why, truthDecks))
            return parsed;
    refusal = problem (400, why);
    return std::nullopt;
}

std::future<juce::Result> EngineControlApi::enqueue (std::function<juce::Result()> run)
{
    auto done = std::make_shared<std::promise<juce::Result>>();
    auto future = done->get_future();
    if (closing)
    {
        done->set_value (juce::Result::fail ("the engine is stopping"));
        return future;
    }
    const std::lock_guard<std::mutex> guard (queueLock);
    queue.push_back ({ std::move (run), done });
    return future;
}

HttpResponse EngineControlApi::edit (const HttpRequest& request)
{
    HttpResponse refusal;
    const auto config = configOf (request, refusal);
    if (! config)
        return refusal;
    enqueue ([this, config = *config] { view.edit (config); return juce::Result::ok(); });
    return json (200, object ({}));
}

HttpResponse EngineControlApi::apply (const HttpRequest& request)
{
    HttpResponse refusal;
    const auto config = configOf (request, refusal);
    if (! config)
        return refusal;
    auto result = enqueue ([this, config = *config] { return view.apply (config); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double> (applyWait.load());
    while (result.wait_for (std::chrono::milliseconds (50)) != std::future_status::ready)
        if (closing || std::chrono::steady_clock::now() > deadline)
            return problem (503, "the engine did not answer");
    const auto done = result.get();
    return done.wasOk() ? json (200, object ({})) : problem (409, done.getErrorMessage());
}

HttpResponse EngineControlApi::filterKind (const HttpRequest& request)
{
    juce::String why;
    const auto body = parseBody (request, why);
    if (! body)
        return problem (400, why);
    const auto kind = (*body)["kind"].toString();
    if (kind != "svf" && kind != "ladder")
        return problem (400, "kind must be \"svf\" or \"ladder\"");
    const auto chosen = kind == "svf" ? a3::FilterKind::svf : a3::FilterKind::ladder;
    enqueue ([this, chosen] { view.setFilterKind (chosen); return juce::Result::ok(); });
    return json (200, object ({}));
}

HttpResponse EngineControlApi::noise (const HttpRequest& request)
{
    juce::String why;
    const auto body = parseBody (request, why);
    const auto burst = body ? noiseFromJson (*body, why) : std::nullopt;
    if (! burst)
        return problem (400, why);
    enqueue ([this, burst = *burst] { view.playNoise (burst); return juce::Result::ok(); });
    return json (200, object ({}));
}

std::optional<std::string> controlBind (const EngineTruth& truth, std::string& why)
{
    if (truth.controlPort < 0 || truth.controlHost.isEmpty())
    {
        why = "the truth has no listener \"engine.control\"";
        return std::nullopt;
    }
    if (truth.controlHost.toStdString() != a3core::loopbackAddress())
    {
        why = "\"engine.control\" is on " + truth.controlHost.toStdString() + ", not on loopback, and it changes the engine's config";
        return std::nullopt;
    }
    return truth.controlHost.toStdString() + ":" + std::to_string (truth.controlPort);
}

std::unique_ptr<a3core::HttpServer> serveEngineControl (EngineControlApi& api, const EngineTruth& truth, const a3core::Journal& journal)
{
    std::string why;
    const auto bind = controlBind (truth, why);
    if (! bind)
    {
        journal ("the control API is not served: " + why, true);
        return nullptr;
    }
    auto server = std::make_unique<a3core::HttpServer> (*bind, api.routes());
    if (! server->start (journal))
    {
        journal ("the control API is not served; the engine runs on without it", true);
        return nullptr;
    }
    return server;
}
}
