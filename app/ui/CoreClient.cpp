// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CoreClient.h"

#include <algorithm>
#include <chrono>
#include <set>

#include "LoopbackHttp.h"

namespace a3app
{
namespace
{
std::string text (const juce::var& value) { return value.toString().toStdString(); }

double number (const juce::var& value) { return value.isVoid() ? 0.0 : static_cast<double> (value); }

long long whole (const juce::var& value) { return value.isVoid() ? 0 : static_cast<juce::int64> (value); }

bool contains (const std::string& haystack, const std::string& needle) { return haystack.find (needle) != std::string::npos; }

HistoryEntry historyEntryFromJson (const juce::var& entry)
{
    return { number (entry["at"]), text (entry["direction"]), text (entry["address"]), text (entry["peer"]),
             valueText (entry["value"]), text (entry["type"]) };
}

std::vector<DeviceTruth> devicesFromJson (const juce::var& rows)
{
    std::vector<DeviceTruth> devices;
    if (const auto* list = rows.getArray())
        for (const auto& row : *list)
            devices.push_back ({ text (row["device"]), text (row["hash"]), static_cast<bool> (row["matches"]), number (row["age"]) });
    return devices;
}

RegisterReport registerFromJson (const juce::var& body)
{
    RegisterReport report;
    report.fetched = true;
    report.fetchedAt = juce::Time::getCurrentTime();
    report.problem = text (body["problem"]);
    if (const auto* list = body["entries"].getArray())
        for (const auto& entry : *list)
            report.entries.push_back ({ text (entry["address"]), text (entry["device"]), text (entry["direction"]), text (entry["source"]),
                                        text (entry["note"]), whole (entry["count"]), static_cast<bool> (entry["seen"]) });
    report.seen = static_cast<int> (whole (body["seen"]));
    report.unseen = static_cast<int> (whole (body["unseen"]));
    return report;
}

juce::String noCoreAt (const juce::String& url) { return "no Core at " + url; }
}

std::string valueText (const juce::var& value)
{
    if (value.isString())
        return text (value);
    if (value.isVoid() || value.isUndefined())
        return {};
    return juce::JSON::toString (value, true).toStdString();
}

std::vector<TrafficRow> rowsFromJson (const juce::var& rows)
{
    std::vector<TrafficRow> out;
    if (const auto* list = rows.getArray())
        for (const auto& row : *list)
        {
            TrafficRow made { text (row["direction"]), text (row["address"]), text (row["peer"]), valueText (row["last_value"]),
                              text (row["last_type"]), whole (row["count"]), std::nullopt, number (row["age"]) };
            if (! row["rate"].isVoid())
                made.rate = number (row["rate"]);
            out.push_back (std::move (made));
        }
    return out;
}

std::optional<TrafficReport> trafficFromJson (const juce::var& body)
{
    if (body.getDynamicObject() == nullptr || body["rows"].getArray() == nullptr)
        return std::nullopt;
    TrafficReport report;
    report.answered = true;
    report.at = number (body["at"]);
    report.rows = rowsFromJson (body["rows"]);
    if (const auto* list = body["history"].getArray())
        for (const auto& entry : *list)
            report.history.push_back (historyEntryFromJson (entry));
    report.unknownAddresses = whole (body["unknown_addresses"]);
    report.unknownMessages = whole (body["unknown_messages"]);
    report.evictedRows = whole (body["evicted"]["rows"]);
    return report;
}

std::vector<TrafficRow> filtered (const std::vector<TrafficRow>& rows, const TrafficFilter& filter)
{
    std::vector<TrafficRow> out;
    for (const auto& row : rows)
    {
        if (! filter.text.empty() && ! contains (row.address, filter.text))
            continue;
        if (filter.onlyActive && row.age >= staleAfterSeconds)
            continue;
        if (! filter.device.empty() && row.peer != filter.device)
            continue;
        out.push_back (row);
    }
    return out;
}

std::vector<HistoryEntry> historyOf (const std::deque<HistoryEntry>& history, const std::string& direction, const std::string& address, size_t limit)
{
    std::vector<HistoryEntry> out;
    for (auto entry = history.rbegin(); entry != history.rend() && out.size() < limit; ++entry)
        if (entry->direction == direction && entry->address == address)
            out.push_back (*entry);
    return out;
}

std::vector<std::string> peersOf (const std::vector<TrafficRow>& rows)
{
    std::set<std::string> names;
    for (const auto& row : rows)
        names.insert (row.peer);
    return { names.begin(), names.end() };
}

// The open /api/stream: the socket, what has arrived of the next event, whether the head is past.
struct CoreClient::Stream
{
    juce::StreamingSocket socket;
    std::string pending;
    bool headDone = false;
};

CoreClient::CoreClient (std::string hostToUse, int portToUse, std::string recallAddress, bool startPolling)
    : host (std::move (hostToUse)), port (portToUse), recallPattern (std::move (recallAddress))
{
    if (startPolling)
        poller = std::thread ([this] { run(); });
}

CoreClient::~CoreClient()
{
    stopping = true;
    wake.notify_all();
    if (poller.joinable())
        poller.join();
}

juce::String CoreClient::url() const { return "http://" + juce::String (host) + ":" + juce::String (port); }

bool CoreClient::answers() const
{
    const auto reply = httpExchange (host, port, "GET", "/api/status", {}, pollTimeoutMillis);
    return reply && reply->status == 200;
}

void CoreClient::run()
{
    using clock = std::chrono::steady_clock;
    auto nextSummary = clock::now();
    while (! stopping)
    {
        {
            const std::lock_guard<std::mutex> round (roundLock);
            fetchRequested();
            if (clock::now() >= nextSummary)
            {
                pollSummary();
                nextSummary = clock::now() + std::chrono::milliseconds (summaryPeriodMillis);
            }
            readTraffic (0);   // what the stream has sent so far; never waits inside the round
        }
        // The wait is outside the round: a refreshNow() on another thread gets its turn (std::mutex is not
        // fair, and a round taken again at once starved the window's first refresh). Core's stream says
        // something every 250 ms; what arrives meanwhile waits in the socket.
        std::unique_lock<std::mutex> sleep (wakeLock);
        wake.wait_for (sleep, std::chrono::milliseconds (roundPeriodMillis), [this] { return stopping.load() || unknownAsked || registerAsked; });
    }
}

void CoreClient::refreshNow (int trafficWaitMillis)
{
    const std::lock_guard<std::mutex> round (roundLock);
    pollSummary();
    fetchRequested();
    readTraffic (trafficWaitMillis);
}

void CoreClient::pollSummary()
{
    const auto status = httpExchange (host, port, "GET", "/api/status", {}, pollTimeoutMillis);
    if (! status || status->status != 200)
    {
        const std::lock_guard<std::mutex> guard (cacheLock);
        summaryCache = {};
        return;
    }
    const auto devices = httpExchange (host, port, "GET", "/api/devices", {}, pollTimeoutMillis);
    const auto rig = httpExchange (host, port, "GET", "/api/rig", {}, pollTimeoutMillis);
    CoreSummary made;
    made.answered = true;
    const auto statusBody = parsedJson (status->body);
    made.audioEngine = text (statusBody["audio_engine"]);
    made.reason = text (statusBody["reason"]);
    if (devices && devices->status == 200)
    {
        const auto body = parsedJson (devices->body);
        made.fingerprint = body["own"].isString() ? text (body["own"]) : std::string();
        made.devices = devicesFromJson (body["rows"]);
    }
    if (rig && rig->status == 200)
        made.rig = parsedJson (rig->body);
    const std::lock_guard<std::mutex> guard (cacheLock);
    summaryCache = std::move (made);
}

void CoreClient::fetchRequested()
{
    if (unknownAsked.exchange (false))
    {
        const auto reply = httpExchange (host, port, "GET", "/api/unknown", {}, sendTimeoutMillis);
        if (reply && reply->status == 200)
        {
            const auto body = parsedJson (reply->body);
            UnknownReport made { true, juce::Time::getCurrentTime(), rowsFromJson (body["rows"]), whole (body["evicted"]) };
            const std::lock_guard<std::mutex> guard (cacheLock);
            unknownCache = std::move (made);
        }
    }
    if (registerAsked.exchange (false))
    {
        const auto reply = httpExchange (host, port, "GET", "/api/register", {}, sendTimeoutMillis);
        if (reply && reply->status == 200)
        {
            auto made = registerFromJson (parsedJson (reply->body));
            const std::lock_guard<std::mutex> guard (cacheLock);
            registerCache = std::move (made);
        }
    }
}

void CoreClient::readTraffic (int waitMillis)
{
    const auto closeStream = [this] {
        stream.reset();
        const std::lock_guard<std::mutex> guard (cacheLock);
        trafficCache.answered = false;
    };
    if (! wantTraffic)
    {
        if (stream != nullptr)
            closeStream();
        return;
    }
    if (stream == nullptr)
    {
        auto opened = std::make_unique<Stream>();
        if (! opened->socket.connect (host, port, pollTimeoutMillis))
            return closeStream();
        const std::string request = "GET /api/stream HTTP/1.0\r\nHost: " + host + "\r\n\r\n";
        if (opened->socket.write (request.data(), (int) request.size()) != (int) request.size())
            return closeStream();
        stream = std::move (opened);
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (waitMillis);
    const auto left = [&] { return (int) std::chrono::duration_cast<std::chrono::milliseconds> (deadline - std::chrono::steady_clock::now()).count(); };
    bool applied = false;
    char buffer[65536];
    for (;;)
    {
        // Until one event came (or the wait is over), then only what is already there.
        const int wait = applied ? 0 : juce::jmax (0, juce::jmin (left(), 100));
        const int ready = stream->socket.waitUntilReady (true, wait);
        if (ready < 0)
            return closeStream();
        if (ready == 0)
        {
            if (applied || left() <= 0)
                return;
            continue;
        }
        const int got = stream->socket.read (buffer, (int) sizeof (buffer), false);
        if (got <= 0)
            return closeStream();
        stream->pending.append (buffer, (size_t) got);
        if (! stream->headDone)
        {
            const auto split = stream->pending.find ("\r\n\r\n");
            if (split == std::string::npos)
                continue;
            if (stream->pending.compare (0, 12, "HTTP/1.1 200") != 0 && stream->pending.compare (0, 12, "HTTP/1.0 200") != 0)
                return closeStream();
            stream->pending.erase (0, split + 4);
            stream->headDone = true;
        }
        for (auto split = stream->pending.find ("\n\n"); split != std::string::npos; split = stream->pending.find ("\n\n"))
        {
            const auto event = stream->pending.substr (0, split);
            stream->pending.erase (0, split + 2);
            static const std::string prefix = "data: ";
            if (event.compare (0, prefix.size(), prefix) == 0)
            {
                applyEvent (event.substr (prefix.size()));
                applied = true;
            }
        }
    }
}

void CoreClient::applyEvent (const std::string& data)
{
    const auto body = parsedJson (data);
    auto report = trafficFromJson (body);
    if (! report)
        return;
    const bool full = static_cast<bool> (body["full"]);
    const std::lock_guard<std::mutex> guard (cacheLock);
    auto history = std::move (trafficCache.history);
    if (full)
        history.clear();
    for (auto& entry : report->history)
        history.push_back (std::move (entry));
    while (history.size() > historyKept)
        history.pop_front();
    report->history = std::move (history);
    trafficCache = std::move (*report);
}

void CoreClient::setTrafficWanted (bool wanted)
{
    wantTraffic = wanted;
    wake.notify_all();
}

void CoreClient::requestUnknown()
{
    unknownAsked = true;
    wake.notify_all();
}

void CoreClient::requestRegister()
{
    registerAsked = true;
    wake.notify_all();
}

TrafficReport CoreClient::traffic() const { const std::lock_guard<std::mutex> g (cacheLock); return trafficCache; }
CoreSummary CoreClient::summary() const { const std::lock_guard<std::mutex> g (cacheLock); return summaryCache; }
UnknownReport CoreClient::unknown() const { const std::lock_guard<std::mutex> g (cacheLock); return unknownCache; }
RegisterReport CoreClient::registerReport() const { const std::lock_guard<std::mutex> g (cacheLock); return registerCache; }
juce::String CoreClient::lastAnswer() const { const std::lock_guard<std::mutex> g (cacheLock); return answer; }

juce::Result CoreClient::send (const std::string& to, const std::string& address, const std::string& valueToSend)
{
    auto* asked = new juce::DynamicObject();
    asked->setProperty ("to", juce::String::fromUTF8 (to.c_str()));
    asked->setProperty ("address", juce::String::fromUTF8 (address.c_str()));
    asked->setProperty ("value", juce::String::fromUTF8 (valueToSend.c_str()));
    const auto body = juce::JSON::toString (juce::var (asked), true).toStdString();
    const auto reply = httpExchange (host, port, "POST", "/api/send", body, sendTimeoutMillis);
    if (! reply)
        return juce::Result::fail (noCoreAt (url()));
    if (reply->timedOut)
        return juce::Result::fail ("Core at " + url() + " did not answer");
    const auto said = parsedJson (reply->body);
    if (reply->status != 200)
        return juce::Result::fail (said["problem"].isString() ? said["problem"].toString() : juce::String ("Core refused"));
    const std::lock_guard<std::mutex> guard (cacheLock);
    answer = juce::String::fromUTF8 ((text (said["address"]) + " " + valueText (said["value"])).c_str());
    return juce::Result::ok();
}

juce::Result CoreClient::recall() { return send ("self", recallPattern, "1"); }

std::unique_ptr<CoreClient> coreClientFor (const a3core::Truth& truth, bool startPolling)
{
    const auto web = truth.endpoint ("core", "web");
    return std::make_unique<CoreClient> (web.host, web.port, truth.pattern ("state.recall"), startPolling);
}
}
