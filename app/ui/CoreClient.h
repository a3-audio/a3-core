// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window's view of Core over Core's HTTP API on loopback (the truth's core.web, CoreApi.h). A background
// thread keeps a cache; every getter reads that cache and never the network, so a page that paints on the
// message thread cannot wait for Core (as RemoteHostView does for the engine).
//
//   traffic   Core's /api/stream at its own 4 Hz, while the monitor is on (setTrafficWanted). The stream
//             and not GET /api/traffic: the traffic snapshot carries the whole history ring (up to 10 000
//             entries) every time, the stream only what is new -- the flood the Python page was built to avoid.
//   summary   /api/devices, /api/status and /api/rig once a second.
//   unknown, register   only when asked for (requestUnknown, requestRegister): they can be tens of thousands
//             of rows, and Core's page never streamed them either.
//   send, recall        the bench: POST /api/send, on the caller's thread, with Core's answer or refusal.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>

#include "Truth.h"

namespace a3app
{
// The Python page's thresholds: a row older than this is not "active", a device silent this long is dead.
constexpr double staleAfterSeconds = 6.0;
constexpr double deadAfterSeconds = 60.0;
constexpr size_t historyKept = 10000;   // the page's HISTORY_CAP, Core's history ring

struct TrafficRow
{
    std::string direction, address, peer, lastValue, lastType;
    long long count = 0;
    std::optional<double> rate;   // per second; none until there was an earlier look
    double age = 0;               // seconds, as of the report
};

struct HistoryEntry
{
    double at = 0;
    std::string direction, address, peer, value, type;
};

struct TrafficReport
{
    bool answered = false;   // Core's stream has said something since it was (re)opened
    double at = 0;           // Core's clock
    std::vector<TrafficRow> rows;
    std::deque<HistoryEntry> history;   // oldest first, at most historyKept
    long long unknownAddresses = 0, unknownMessages = 0, evictedRows = 0;
};

struct DeviceTruth
{
    std::string device, hash;
    bool matches = false;   // speaks Core's a3-osc.json
    double age = 0;
};

struct CoreSummary
{
    bool answered = false;
    std::string fingerprint;          // Core's own a3-osc.json
    std::vector<DeviceTruth> devices;
    std::string audioEngine, reason;
    juce::var rig;
};

struct UnknownReport
{
    bool fetched = false;
    juce::Time fetchedAt;
    std::vector<TrafficRow> rows;
    long long evicted = 0;
};

struct RegisterEntry
{
    std::string address, device, direction, source, note;
    long long count = 0;
    bool seen = false;
};

struct RegisterReport
{
    bool fetched = false;
    juce::Time fetchedAt;
    std::string problem;
    std::vector<RegisterEntry> entries;
    int seen = 0, unseen = 0;
};

// What the TRAFFIC page narrows the rows to: a piece of the address, only rows younger than
// staleAfterSeconds, one peer. An empty field narrows nothing.
struct TrafficFilter
{
    std::string text;
    bool onlyActive = false;
    std::string device;
};
std::vector<TrafficRow> filtered (const std::vector<TrafficRow>&, const TrafficFilter&);
// The selected row's history, newest first, at most `limit`.
std::vector<HistoryEntry> historyOf (const std::deque<HistoryEntry>&, const std::string& direction, const std::string& address, size_t limit);
// The peers the rows name, sorted, once each.
std::vector<std::string> peersOf (const std::vector<TrafficRow>&);

// What the JSON of Core's routes reads as (exposed for the tests).
std::optional<TrafficReport> trafficFromJson (const juce::var&);
std::vector<TrafficRow> rowsFromJson (const juce::var& rows);
std::string valueText (const juce::var&);   // a value as the page shows it: text bare, anything else as JSON

class CoreClient
{
public:
    static constexpr int pollTimeoutMillis = 500;
    static constexpr int sendTimeoutMillis = 2000;
    static constexpr int summaryPeriodMillis = 1000;
    static constexpr int roundPeriodMillis = 100;   // how often the thread reads what the stream sent

    // `recallAddress` is the truth's state.recall pattern. `startPolling` false: nothing runs until
    // refreshNow() (the tests drive it).
    CoreClient (std::string host, int port, std::string recallAddress, bool startPolling = true);
    ~CoreClient();

    CoreClient (const CoreClient&) = delete;
    CoreClient& operator= (const CoreClient&) = delete;

    // One round on the caller's thread: the summary, what was asked for, and the traffic stream's events
    // (waiting up to `trafficWaitMillis` for one when the traffic is wanted).
    void refreshNow (int trafficWaitMillis = 1000);
    // Whether Core answers at all, asked now (GET /api/status).
    bool answers() const;
    juce::String url() const;

    // Off: the stream is closed and the traffic not read (the monitor is hidden).
    void setTrafficWanted (bool);
    bool trafficWanted() const { return wantTraffic; }
    void requestUnknown();
    void requestRegister();

    TrafficReport traffic() const;
    CoreSummary summary() const;
    UnknownReport unknown() const;
    RegisterReport registerReport() const;

    // POST /api/send: ok, and lastAnswer() says what Core sent; or Core's refusal as the failure.
    juce::Result send (const std::string& to, const std::string& address, const std::string& valueText);
    juce::Result recall();   // the truth's state.recall with 1, to Core itself
    juce::String lastAnswer() const;
    const std::string& recallAddress() const { return recallPattern; }

private:
    struct Stream;

    void run();
    void pollSummary();
    void fetchRequested();
    void readTraffic (int waitMillis);
    void applyEvent (const std::string& data);

    const std::string host;
    const int port;
    const std::string recallPattern;
    std::atomic<bool> wantTraffic { true }, unknownAsked { false }, registerAsked { false };
    std::mutex roundLock;   // one round at a time: the thread's or refreshNow's
    std::unique_ptr<Stream> stream;
    mutable std::mutex cacheLock;
    TrafficReport trafficCache;
    CoreSummary summaryCache;
    UnknownReport unknownCache;
    RegisterReport registerCache;
    juce::String answer;
    std::atomic<bool> stopping { false };
    std::mutex wakeLock;
    std::condition_variable wake;
    std::thread poller;
};

// The client of the truth's core.web (an `any` host is reached on loopback), recalling with state.recall.
std::unique_ptr<CoreClient> coreClientFor (const a3core::Truth&, bool startPolling = true);
}
