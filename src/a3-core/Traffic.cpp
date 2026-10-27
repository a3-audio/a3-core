// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Traffic.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

#include "Json.h"

namespace a3core
{
namespace
{
// B1's osc_bytes counts a list as "anything else a single word carries".
const OscArg listWord = pyInt (0);

int indexOf (Direction direction) { return direction == Direction::in ? 0 : 1; }

// OSC aligns every field to four bytes.
long long padded (size_t length) { return static_cast<long long> ((length + 3) & ~size_t (3)); }

// bytes.__repr__
std::string bytesRepr (const std::vector<uint8_t>& bytes)
{
    const bool hasSingle = std::find (bytes.begin(), bytes.end(), uint8_t ('\'')) != bytes.end();
    const bool hasDouble = std::find (bytes.begin(), bytes.end(), uint8_t ('"')) != bytes.end();
    const char quote = hasSingle && ! hasDouble ? '"' : '\'';
    static const char hex[] = "0123456789abcdef";

    std::string out = "b";
    out += quote;
    for (const auto byte : bytes)
    {
        if (byte == static_cast<uint8_t> (quote) || byte == '\\')
            out += std::string ("\\") + static_cast<char> (byte);
        else if (byte == '\t')
            out += "\\t";
        else if (byte == '\n')
            out += "\\n";
        else if (byte == '\r')
            out += "\\r";
        else if (byte < 0x20 || byte >= 0x7f)
            out += std::string ("\\x") + hex[byte >> 4] + hex[byte & 15];
        else
            out += static_cast<char> (byte);
    }
    return out + quote;
}

std::optional<double> floatOf (const OscArg& arg)
{
    if (const auto* single = std::get_if<float> (&arg.value))
        return static_cast<double> (*single);
    if (const auto* wide = std::get_if<double> (&arg.value))
        return *wide;
    return std::nullopt;
}

// a3_core_web._jsonable: what json.dumps keeps of a value. A float that is not finite would be written
// as a bare NaN and break a strict parser, and the tables never evict a row for being odd, so it
// becomes its repr; so does anything JSON cannot hold.
juce::var jsonableOf (const std::optional<OscArg>& value)
{
    if (! value)
        return {};
    const auto& arg = *value;
    switch (arg.tag)
    {
        case 'i': case 'h': case 'r': case 't':
            if (const auto* small = std::get_if<int32_t> (&arg.value))
                return juce::var (static_cast<juce::int64> (*small));
            if (const auto* big = std::get_if<int64_t> (&arg.value))
                return juce::var (static_cast<juce::int64> (*big));
            break;
        case 'f': case 'd':
            if (const auto number = floatOf (arg))
                return std::isfinite (*number) ? juce::var (*number) : juce::var (juce::String (pythonRepr (*number)));
            break;
        case 's':
            if (const auto* text = std::get_if<std::string> (&arg.value))
                return juce::var (juce::String::fromUTF8 (text->data(), static_cast<int> (text->size())));
            break;
        case 'T': return juce::var (true);
        case 'F': return juce::var (false);
        case 'N': return {};
        case 'b':
            if (const auto* bytes = std::get_if<std::vector<uint8_t>> (&arg.value))
                return juce::var (juce::String (bytesRepr (*bytes)));
            break;
        case 'm':
            if (const auto* bytes = std::get_if<std::vector<uint8_t>> (&arg.value))
            {
                std::string out = "(";
                for (size_t i = 0; i < bytes->size(); ++i)
                    out += (i > 0 ? ", " : "") + std::to_string ((*bytes)[i]);
                return juce::var (juce::String (out + (bytes->size() == 1 ? ",)" : ")")));
            }
            break;
        default: break;
    }
    return {};
}

// str.__repr__ of a Python str.
std::string strRepr (const std::string& value)
{
    const bool hasSingle = value.find ('\'') != std::string::npos;
    const bool hasDouble = value.find ('"') != std::string::npos;
    const char quote = hasSingle && ! hasDouble ? '"' : '\'';
    static const char hex[] = "0123456789abcdef";

    std::string out (1, quote);
    for (const auto character : value)
    {
        const auto byte = static_cast<uint8_t> (character);
        if (character == quote || character == '\\')
            out += std::string ("\\") + character;
        else if (character == '\t')
            out += "\\t";
        else if (character == '\n')
            out += "\\n";
        else if (character == '\r')
            out += "\\r";
        else if (byte < 0x20 || byte == 0x7f)
            out += std::string ("\\x") + hex[byte >> 4] + hex[byte & 15];
        else
            out += character;   // UTF-8 goes through: Python prints printable non-ASCII as it is
    }
    return out + quote;
}

// repr() of one OSC argument as python-osc hands it on.
std::string argRepr (const OscArg& arg)
{
    switch (arg.tag)
    {
        case 'i': case 'h': case 'r': case 't':
            if (const auto* small = std::get_if<int32_t> (&arg.value))
                return std::to_string (*small);
            if (const auto* big = std::get_if<int64_t> (&arg.value))
                return std::to_string (*big);
            break;
        case 'f': case 'd':
            if (const auto number = floatOf (arg))
                return pythonRepr (*number);
            break;
        case 's':
            if (const auto* text = std::get_if<std::string> (&arg.value))
                return strRepr (*text);
            break;
        case 'T': return "True";
        case 'F': return "False";
        case 'b':
            if (const auto* bytes = std::get_if<std::vector<uint8_t>> (&arg.value))
                return bytesRepr (*bytes);
            break;
        case 'm':
            if (const auto* bytes = std::get_if<std::vector<uint8_t>> (&arg.value))
            {
                std::string out = "(";
                for (size_t i = 0; i < bytes->size(); ++i)
                    out += (i > 0 ? ", " : "") + std::to_string ((*bytes)[i]);
                return out + (bytes->size() == 1 ? ",)" : ")");
            }
            break;
        default: break;
    }
    return "None";
}

// a3_core_web._jsonable of a list: a list is not str, int, bool or None, so it is its repr.
juce::var listRepr (const ListValue& list)
{
    std::string out = "[";
    for (size_t i = 0; i < list.size(); ++i)
        out += (i > 0 ? ", " : "") + argRepr (list[i]);
    return juce::var (juce::String::fromUTF8 ((out + "]").c_str()));
}

juce::var jsonableOf (const std::optional<OscArg>& value, const std::optional<ListValue>& list)
{
    return list ? listRepr (*list) : jsonableOf (value);
}

juce::var text (const std::string& utf8) { return juce::var (juce::String::fromUTF8 (utf8.data(), static_cast<int> (utf8.size()))); }

juce::var objectOf (std::initializer_list<std::pair<const char*, juce::var>> members)
{
    auto* made = new juce::DynamicObject();
    for (const auto& member : members)
        made->setProperty (juce::Identifier (member.first), member.second);
    return juce::var (made);
}

struct PeerTotals
{
    std::string peer, direction;
    double rate = 0, bytesPerSecond = 0;
};

juce::var line (double rate, double bytesPerSecond) { return objectOf ({ { "rate", rate }, { "kbit", bytesPerSecond * 8 / 1000 } }); }
}

const char* directionName (Direction direction) { return direction == Direction::in ? "in" : "out"; }

const std::vector<std::string> commanders { "mixer", "motion" };
const std::vector<std::string> answerers { "reaper" };

const char* pythonTypeName (const OscArg* value)
{
    if (value == nullptr)
        return "NoneType";
    switch (value->tag)
    {
        case 'i': case 'h': case 'r': case 't': return "int";
        case 'f': case 'd': return "float";
        case 's': return "str";
        case 'T': case 'F': return "bool";
        case 'b': return "bytes";
        case 'm': return "tuple";
        default: return "NoneType";
    }
}

long long oscBytes (const std::string& address, const OscArg* value)
{
    // The type tag is "," and at most one letter: four bytes either way once the null and the padding
    // are added.
    long long size = padded (address.size() + 1) + 4;
    if (value == nullptr || value->tag == 'N' || value->tag == 'T' || value->tag == 'F')
        return size;
    if (value->tag == 's')
    {
        const auto* text = std::get_if<std::string> (&value->value);
        return size + padded ((text != nullptr ? text->size() : 0) + 1);
    }
    if (value->tag == 'b')
    {
        const auto* bytes = std::get_if<std::vector<uint8_t>> (&value->value);
        return size + 4 + padded (bytes != nullptr ? bytes->size() : 0);
    }
    // int32, float32 and anything else a single word carries.
    return size + 4;
}

std::string peerName (const std::string& host, const std::map<std::string, std::string>& peers,
                      const std::optional<std::vector<std::string>>& only)
{
    int matches = 0;
    const std::string* matched = nullptr;
    for (const auto& [name, at] : peers)
    {
        if (only && std::find (only->begin(), only->end(), name) == only->end())
            continue;
        if (at != host)
            continue;
        ++matches;
        matched = &name;
    }
    if (matches == 1)
        return *matched;
    if (matches > 1)
        return host + " (mehrdeutig)";
    return host;
}

Traffic::Traffic (size_t historyToUse, size_t rowsToUse, size_t unknownToUse)
    : historyLimit (historyToUse), rowLimit (rowsToUse), unknownLimit (unknownToUse),
      lastStamp (-std::numeric_limits<double>::infinity())
{
}

// Stamped inside the lock, no earlier than any stamp before it and never equal to one: snapshot() and
// seen() both stamp, and the delta stream (historySince) relies on the stamps following the order of
// the lock. The caller read `now` before the lock, so a late thread may hand in an older one; clamping
// it up keeps the order, and moving a tie up by one step keeps an entry from reading as "not newer" than
// the snapshot it belongs after -- the one failure Python only noted as unobserved.
double Traffic::stamp (double now)
{
    lastStamp = std::max (now, std::nextafter (lastStamp, std::numeric_limits<double>::infinity()));
    return lastStamp;
}

void Traffic::dropUnknown (const std::string& address)
{
    const auto found = unknownIndex.find (address);
    if (found == unknownIndex.end())
        return;
    unknownRows.erase (found->second);
    unknownIndex.erase (found);
}

void Traffic::trimRows()
{
    while (rows.size() > rowLimit)
    {
        const auto& oldest = rows.front();
        rowIndex[indexOf (oldest.direction)].erase (oldest.address);
        rows.pop_front();
        ++evictedRows;
    }
}

void Traffic::trimUnknown()
{
    while (unknownRows.size() > unknownLimit)
    {
        unknownIndex.erase (unknownRows.front().address);
        unknownRows.pop_front();
        ++evictedUnknown;
    }
}

void Traffic::seen (Direction direction, const std::string& address, const OscArg* value, const std::string& peer, double now)
{
    noteSeen (direction, address, value, nullptr, peer, now);
}

void Traffic::seenList (Direction direction, const std::string& address, const ListValue& value, const std::string& peer, double now)
{
    noteSeen (direction, address, nullptr, &value, peer, now);
}

void Traffic::noteSeen (Direction direction, const std::string& address, const OscArg* value, const ListValue* list,
                        const std::string& peer, double now)
{
    const char* typeName = list != nullptr ? "list" : pythonTypeName (value);

    std::lock_guard<std::mutex> guard (lock);
    const double at = stamp (now);

    auto& index = rowIndex[indexOf (direction)];
    auto found = index.find (address);
    Rows::iterator row;
    if (found == index.end())
    {
        TrafficRow made;
        made.direction = direction;
        made.address = address;
        row = rows.insert (rows.end(), std::move (made));
        index.emplace (address, row);
        ++madeCount;

        // An address Core could not route before and can now: the unknown table survives a restart
        // while what Core can route changes between restarts. Only on the branch that creates a row
        // and only inbound -- `unknown` is called from the feedback path alone, so an outgoing row says
        // nothing about whether the same address is understood coming back, and a repeat message must
        // not pay for a lookup it can never need.
        if (direction == Direction::in)
            dropUnknown (address);
    }
    else
    {
        row = found->second;
        rows.splice (rows.end(), rows, row);
    }

    ++row->count;
    row->bytes += list != nullptr ? oscBytes (address, &listWord) : oscBytes (address, value);
    row->lastValue = value != nullptr ? std::optional<OscArg> (*value) : std::nullopt;
    row->lastList = list != nullptr ? std::optional<ListValue> (*list) : std::nullopt;
    row->lastType.assign (typeName);
    row->lastSeen = at;
    row->peer = peer;

    trimRows();

    if (historyLimit == 0)
        return;
    // Built from what was handed in: with a row cap of zero the row is already gone.
    history.push_back ({ at, direction, address, value != nullptr ? std::optional<OscArg> (*value) : std::nullopt,
                         list != nullptr ? std::optional<ListValue> (*list) : std::nullopt, typeName, peer });
    if (history.size() > historyLimit)
        history.pop_front();
}

void Traffic::unknown (const std::string& address, const OscArg* value, const std::string& peer, double now)
{
    noteUnknown (address, value, nullptr, peer, now);
}

void Traffic::unknownList (const std::string& address, const ListValue& value, const std::string& peer, double now)
{
    noteUnknown (address, nullptr, &value, peer, now);
}

void Traffic::noteUnknown (const std::string& address, const OscArg* value, const ListValue* list, const std::string& peer, double now)
{
    const char* typeName = list != nullptr ? "list" : pythonTypeName (value);

    std::lock_guard<std::mutex> guard (lock);
    const double at = stamp (now);

    auto found = unknownIndex.find (address);
    Unknowns::iterator row;
    if (found == unknownIndex.end())
    {
        UnknownRow made;
        made.address = address;
        row = unknownRows.insert (unknownRows.end(), std::move (made));
        unknownIndex.emplace (address, row);
        ++madeCount;
    }
    else
    {
        row = found->second;
        unknownRows.splice (unknownRows.end(), unknownRows, row);
    }

    ++row->count;
    row->lastValue = value != nullptr ? std::optional<OscArg> (*value) : std::nullopt;
    row->lastList = list != nullptr ? std::optional<ListValue> (*list) : std::nullopt;
    row->lastType.assign (typeName);
    row->lastSeen = at;
    row->peer = peer;
    // A lifetime figure kept beside the row's own count, not summed over the table on a snapshot: that
    // sum is paid on every look, inside the lock every OSC thread has to take.
    ++unknownMessagesTotal;

    trimUnknown();
}

TrafficSnapshot Traffic::snapshot (std::optional<double> historySince, double now)
{
    TrafficSnapshot out;
    std::lock_guard<std::mutex> guard (lock);
    // Stamped first thing inside the lock, like seen(): see stamp().
    out.at = stamp (now);
    out.rows.assign (rows.begin(), rows.end());
    if (! historySince)
        out.history.assign (history.begin(), history.end());
    else
        for (const auto& entry : history)
            if (entry.at > *historySince)
                out.history.push_back (entry);
    out.unknownAddresses = unknownRows.size();
    out.unknownMessages = unknownMessagesTotal;
    out.evictedRows = evictedRows;
    out.evictedUnknown = evictedUnknown;
    return out;
}

UnknownSnapshot Traffic::unknownSnapshot (double now)
{
    UnknownSnapshot out;
    std::lock_guard<std::mutex> guard (lock);
    out.at = stamp (now);
    out.rows.assign (unknownRows.begin(), unknownRows.end());
    out.evicted = evictedUnknown;
    return out;
}

long long Traffic::created() const
{
    std::lock_guard<std::mutex> guard (lock);
    return madeCount;
}

int Traffic::restore (const std::vector<RestoredRow>& savedRows, const std::vector<RestoredUnknown>& savedUnknown,
                      double wallNow, double monoNow)
{
    // The saved times are wall clock and ours are monotonic. A file from the future -- a clock corrected
    // backwards, or a file copied off another machine -- would otherwise make rows younger than the
    // moment they are read in; those are clamped to now, the mild lie, rather than a negative age.
    const auto when = [&] (double saved) { return monoNow - std::max (0.0, wallNow - saved); };

    int put = 0;
    std::lock_guard<std::mutex> guard (lock);
    for (const auto& saved : savedRows)
    {
        auto& index = rowIndex[indexOf (saved.direction)];
        if (index.count (saved.address) > 0)
            continue;
        // No byte count survives a restart: the window makes rates from differences, so where the count
        // starts does not matter, only that there is one for seen() to add to.
        TrafficRow row;
        row.direction = saved.direction;
        row.address = saved.address;
        row.count = saved.count;
        row.lastSeen = when (saved.savedAt);
        row.peer = saved.peer;
        index.emplace (saved.address, rows.insert (rows.end(), std::move (row)));
        ++madeCount;
        ++put;
    }
    trimRows();

    for (const auto& saved : savedUnknown)
    {
        if (unknownIndex.count (saved.address) > 0)
            continue;
        // Saved as unrecognised, and routed since: the creating branch of seen() cannot reach these,
        // the known row is already here. Inbound only, and after the known rows are in, which the order
        // of these two loops guarantees.
        if (rowIndex[0].count (saved.address) > 0)
            continue;
        UnknownRow row;
        row.address = saved.address;
        row.count = saved.count;
        row.lastSeen = when (saved.savedAt);
        row.peer = saved.peer;
        unknownIndex.emplace (saved.address, unknownRows.insert (unknownRows.end(), std::move (row)));
        // The running total is a lifetime figure the page shows beside the address count: restoring the
        // rows without it would put a table of 19,335 addresses beside a total of nought.
        unknownMessagesTotal += saved.count;
        ++madeCount;
        ++put;
    }
    trimUnknown();
    return put;
}

juce::var snapshotAsJson (const TrafficSnapshot& snapshot, const TrafficSnapshot* previous)
{
    struct Before
    {
        long long count, bytes;
    };
    std::map<std::pair<int, std::string>, Before> before;
    double span = 0;
    if (previous != nullptr)
    {
        span = snapshot.at - previous->at;
        for (const auto& row : previous->rows)
            before[{ indexOf (row.direction), row.address }] = { row.count, row.bytes };
    }

    std::vector<PeerTotals> sums;
    std::map<std::pair<std::string, std::string>, size_t> sumAt;

    juce::Array<juce::var> rows;
    for (const auto& row : snapshot.rows)
    {
        const auto was = before.find ({ indexOf (row.direction), row.address });
        const bool hasRate = was != before.end() && span > 0;
        const double rate = hasRate ? static_cast<double> (row.count - was->second.count) / span : 0;

        rows.add (objectOf ({ { "direction", directionName (row.direction) },
                              { "address", text (row.address) },
                              { "count", static_cast<juce::int64> (row.count) },
                              { "bytes", static_cast<juce::int64> (row.bytes) },
                              { "last_value", jsonableOf (row.lastValue, row.lastList) },
                              { "last_type", text (row.lastType) },
                              { "last_seen", row.lastSeen },
                              { "peer", text (row.peer) },
                              { "age", snapshot.at - row.lastSeen },
                              { "rate", hasRate ? juce::var (rate) : juce::var() } }));

        // A negative difference is a row evicted and made again in between; its rate is shown as the row
        // always showed it, but it must not pull a peer's total below what actually arrived.
        if (! hasRate || rate < 0)
            continue;
        const auto key = std::make_pair (row.peer, std::string (directionName (row.direction)));
        auto at = sumAt.find (key);
        if (at == sumAt.end())
        {
            at = sumAt.emplace (key, sums.size()).first;
            sums.push_back ({ key.first, key.second, 0, 0 });
        }
        sums[at->second].rate += rate;
        sums[at->second].bytesPerSecond += static_cast<double> (std::max (0LL, row.bytes - was->second.bytes)) / span;
    }

    juce::Array<juce::var> entries;
    for (const auto& entry : snapshot.history)
        entries.add (objectOf ({ { "at", entry.at },
                                 { "direction", directionName (entry.direction) },
                                 { "address", text (entry.address) },
                                 { "value", jsonableOf (entry.value, entry.list) },
                                 { "type", text (entry.type) },
                                 { "peer", text (entry.peer) },
                                 { "age", snapshot.at - entry.at } }));

    juce::var totals, overall;
    if (previous != nullptr)
    {
        // Messages per second and kbit/s per peer and direction, and overall. kbit is the OSC payload
        // (oscBytes): what the rig puts on the switch, without UDP and IP headers.
        auto ordered = sums;
        std::sort (ordered.begin(), ordered.end(), [] (const PeerTotals& a, const PeerTotals& b) {
            return std::tie (a.direction, a.peer) < std::tie (b.direction, b.peer);
        });
        juce::Array<juce::var> lines;
        for (const auto& sum : ordered)
        {
            auto entry = line (sum.rate, sum.bytesPerSecond);
            entry.getDynamicObject()->setProperty ("peer", text (sum.peer));
            entry.getDynamicObject()->setProperty ("direction", text (sum.direction));
            lines.add (entry);
        }
        totals = juce::var (lines);

        const auto overallOf = [&] (const char* direction) {
            double rate = 0, bytes = 0;
            for (const auto& sum : sums)
                if (sum.direction == direction)
                {
                    rate += sum.rate;
                    bytes += sum.bytesPerSecond;
                }
            return line (rate, bytes);
        };
        overall = objectOf ({ { "in", overallOf ("in") }, { "out", overallOf ("out") } });
    }

    return objectOf ({ { "at", snapshot.at },
                       { "rows", juce::var (rows) },
                       { "history", juce::var (entries) },
                       { "unknown_addresses", static_cast<juce::int64> (snapshot.unknownAddresses) },
                       { "unknown_messages", static_cast<juce::int64> (snapshot.unknownMessages) },
                       { "evicted", objectOf ({ { "rows", static_cast<juce::int64> (snapshot.evictedRows) },
                                                { "unknown", static_cast<juce::int64> (snapshot.evictedUnknown) } }) },
                       { "full", previous == nullptr },
                       { "totals", totals },
                       { "overall", overall } });
}

juce::var unknownAsJson (const UnknownSnapshot& unknown)
{
    juce::Array<juce::var> rows;
    for (const auto& row : unknown.rows)
        rows.add (objectOf ({ { "address", text (row.address) },
                              { "count", static_cast<juce::int64> (row.count) },
                              { "last_value", jsonableOf (row.lastValue, row.lastList) },
                              { "last_type", text (row.lastType) },
                              { "last_seen", row.lastSeen },
                              { "peer", text (row.peer) },
                              { "age", unknown.at - row.lastSeen },
                              { "rate", juce::var() },
                              { "direction", "in" },
                              { "unknown", true } }));
    return objectOf ({ { "at", unknown.at },
                       { "rows", juce::var (rows) },
                       { "evicted", static_cast<juce::int64> (unknown.evicted) } });
}
}
