// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_traffic (B0 f3345dc7feeb): what went past Core, counted -- and nothing more on the
// hot path. `seen` runs about 2300 times a second on the rig: a lookup, a splice, a ring push, under
// one mutex (the serve thread writes, HTTP threads read snapshots: P8). No formatting, no rate, no
// time series: the rate belongs to whoever reads two snapshots. Also the serialisers of the monitor
// half of a3_core_web (as_json, unknown_as_json), which only turn a snapshot into JSON.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <juce_core/juce_core.h>

#include "OscWire.h"

namespace a3core
{
enum class Direction { in, out };

// "in" / "out", as the snapshots and the files spell them.
const char* directionName (Direction direction);

// The devices that can have sent a message on Core's main port, and the ones that can have sent on
// REAPER's feedback port. Disjoint, which is the point: given as `only`, a shared host stops being
// ambiguous.
extern const std::vector<std::string> commanders;
extern const std::vector<std::string> answerers;

// How many bytes one message takes as an OSC packet, worked out and not measured: the address and the
// first argument are all `seen` is handed. nullptr is a message without arguments.
long long oscBytes (const std::string& address, const OscArg* value);

// The device name a host stands for, or the host itself. `peers` maps a device to its host. With
// `only`, the candidates are narrowed to those names first. More than one match: the host plus
// " (mehrdeutig)" -- the spelling is B0's, German, and kept.
std::string peerName (const std::string& host, const std::map<std::string, std::string>& peers,
                      const std::optional<std::vector<std::string>>& only = std::nullopt);

// Python's type(value).__name__ of what the OSC argument becomes: int, float, str, bool, bytes,
// tuple, NoneType (nullptr too).
const char* pythonTypeName (const OscArg* value);

// What a message of several arguments carried: a Python list (B1 records list(osc_arguments)).
using ListValue = std::vector<OscArg>;

struct TrafficRow
{
    Direction direction = Direction::in;
    std::string address;
    long long count = 0, bytes = 0;
    std::optional<OscArg> lastValue;
    std::optional<ListValue> lastList;   // set instead of lastValue; lastType is "list"
    std::string lastType;
    double lastSeen = 0;
    std::string peer;
};

struct HistoryEntry
{
    double at = 0;
    Direction direction = Direction::in;
    std::string address;
    std::optional<OscArg> value;
    std::optional<ListValue> list;
    std::string type, peer;
};

struct UnknownRow
{
    std::string address;
    long long count = 0;
    std::optional<OscArg> lastValue;
    std::optional<ListValue> lastList;
    std::string lastType;
    double lastSeen = 0;
    std::string peer;
};

struct TrafficSnapshot
{
    double at = 0;
    std::vector<TrafficRow> rows;
    std::vector<HistoryEntry> history;
    size_t unknownAddresses = 0;
    long long unknownMessages = 0, evictedRows = 0, evictedUnknown = 0;
};

struct UnknownSnapshot
{
    double at = 0;
    std::vector<UnknownRow> rows;
    long long evicted = 0;
};

// A row of the saved list. `savedAt` is the wall clock of the file.
struct RestoredRow
{
    Direction direction = Direction::in;
    std::string address;
    long long count = 0;
    std::string peer;
    double savedAt = 0;
};

struct RestoredUnknown
{
    std::string address;
    long long count = 0;
    std::string peer;
    double savedAt = 0;
};

class Traffic
{
public:
    // What the ring holds: about two minutes at a hundred messages a second.
    static constexpr size_t historyCap = 10000;
    // Understood addresses: A3's vocabulary is about forty, so this guards against a sender inventing
    // addresses and is no working limit.
    static constexpr size_t rowCap = 5000;
    // Unknown addresses: one REAPER project reports 19,335 when its surface connects.
    static constexpr size_t unknownCap = 50000;

    explicit Traffic (size_t history = historyCap, size_t rows = rowCap, size_t unknown = unknownCap);

    // Note one message. `now` is the monotonic clock the caller read; it is stamped no earlier than the
    // last stamp of any call, so the stamps follow the order of the lock -- see stamp().
    void seen (Direction direction, const std::string& address, const OscArg* value, const std::string& peer, double now);

    // The same for a message whose value is a Python list (several arguments; the pointers always): the
    // row holds the list, its type is "list", and its size in bytes is that of one word, as B1 counts a
    // list.
    void seenList (Direction direction, const std::string& address, const ListValue& value, const std::string& peer, double now);

    // Note a message Core received and could not route. A full row, not a bare count, but never part
    // of snapshot(): REAPER alone reports 19,335 of them.
    void unknown (const std::string& address, const OscArg* value, const std::string& peer, double now);
    void unknownList (const std::string& address, const ListValue& value, const std::string& peer, double now);

    // A whole, consistent copy. `historySince` makes `history` incremental: only entries stamped later.
    TrafficSnapshot snapshot (std::optional<double> historySince, double now);
    UnknownSnapshot unknownSnapshot (double now);

    // How many addresses either table has ever taken in; a writer watches it to know the list changed.
    long long created() const;

    // Put a saved address list back without overwriting what is live. The saved times are wall clock;
    // they become this process's monotonic times (a clock that went backwards reads "just seen").
    // Returns how many rows were put.
    int restore (const std::vector<RestoredRow>& rows, const std::vector<RestoredUnknown>& unknownRows,
                 double wallNow, double monoNow);

private:
    using Rows = std::list<TrafficRow>;
    using Unknowns = std::list<UnknownRow>;

    void noteSeen (Direction direction, const std::string& address, const OscArg* value, const ListValue* list,
                   const std::string& peer, double now);
    void noteUnknown (const std::string& address, const OscArg* value, const ListValue* list, const std::string& peer, double now);
    double stamp (double now);
    void dropUnknown (const std::string& address);
    void trimRows();
    void trimUnknown();

    mutable std::mutex lock;
    Rows rows;
    Unknowns unknownRows;
    std::unordered_map<std::string, Rows::iterator> rowIndex[2];
    std::unordered_map<std::string, Unknowns::iterator> unknownIndex;
    std::deque<HistoryEntry> history;
    size_t historyLimit, rowLimit, unknownLimit;
    long long evictedRows = 0, evictedUnknown = 0, madeCount = 0, unknownMessagesTotal = 0;
    double lastStamp;
};

// a3_core_web.as_json: a snapshot ready for the browser, with a rate per row and totals per peer and
// direction. `previous` is the earlier look, if there was one: the first look gets no rate (null, not
// zero -- zero would say that nothing arrives) and no totals.
juce::var snapshotAsJson (const TrafficSnapshot& snapshot, const TrafficSnapshot* previous);

// a3_core_web.unknown_as_json: shaped like the rows of snapshotAsJson, with rate null, direction "in"
// and unknown true.
juce::var unknownAsJson (const UnknownSnapshot& unknown);
}
