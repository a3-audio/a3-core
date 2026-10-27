// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Register.h"

#include <algorithm>
#include <cstdint>
#include <memory>

#include "Json.h"

namespace a3core::reg
{
namespace
{
juce::var object() { return juce::var (new juce::DynamicObject()); }

juce::var emptyRegister (const juce::String& problem)
{
    auto empty = object();
    auto* made = empty.getDynamicObject();
    made->setProperty ("problem", problem);
    made->setProperty ("devices", juce::var (juce::Array<juce::var>()));
    made->setProperty ("directions", juce::var (juce::Array<juce::var>()));
    made->setProperty ("entries", juce::var (juce::Array<juce::var>()));
    return empty;
}

// The names of a list, the strings of it: the others are not names.
juce::var namesIn (const juce::var& list)
{
    juce::Array<juce::var> names;
    if (const auto* items = list.getArray())
        for (const auto& item : *items)
            if (item.isString())
                names.add (item);
    return juce::var (names);
}

bool isDigit (char c) { return c >= '0' && c <= '9'; }

// A piece of a template: a literal, one value inside one segment, or OSC's wildcard.
struct Piece
{
    enum Kind { literal, slot, star } kind;
    std::string text;
};

std::vector<Piece> piecesOf (const std::string& pattern)
{
    std::vector<Piece> pieces;
    const auto add = [&] (Piece::Kind kind, char c = 0) {
        if (kind == Piece::literal)
        {
            if (pieces.empty() || pieces.back().kind != Piece::literal)
                pieces.push_back ({ Piece::literal, "" });
            pieces.back().text += c;
        }
        else
            pieces.push_back ({ kind, "" });
    };
    for (size_t i = 0; i < pattern.size(); ++i)
    {
        const char c = pattern[i];
        if (c == '*')
            add (Piece::star);
        else if (c == '@')
            add (Piece::slot);
        else if (c == '{' && pattern.find ('}', i + 1) != std::string::npos)
        {
            // `{ch}` as this system writes it: everything up to the first closing brace.
            add (Piece::slot);
            i = pattern.find ('}', i + 1);
        }
        else
            add (Piece::literal, c);
    }
    return pieces;
}

// Whether the pieces cover s[0, length) exactly. Slot = [^/]+, star = .* (no newline), literals are
// themselves. Bytes stand in for characters: '/' and '\n' are ASCII, so a multi-byte character is a run
// of bytes that is neither.
bool covers (const std::vector<Piece>& pieces, const std::string& s, size_t length)
{
    const size_t width = length + 1;
    std::vector<int8_t> memo ((pieces.size() + 1) * width, -1);

    const std::function<bool (size_t, size_t)> from = [&] (size_t piece, size_t at) -> bool {
        auto& known = memo[piece * width + at];
        if (known >= 0)
            return known == 1;
        bool ok = false;
        if (piece == pieces.size())
            ok = at == length;
        else if (pieces[piece].kind == Piece::literal)
        {
            const auto& literal = pieces[piece].text;
            ok = s.compare (at, literal.size(), literal) == 0 && at + literal.size() <= length && from (piece + 1, at + literal.size());
        }
        else
        {
            const bool isSlot = pieces[piece].kind == Piece::slot;
            // Longest first: the first success decides, and the table keeps the rest cheap.
            size_t reach = at;
            while (reach < length && (isSlot ? s[reach] != '/' : s[reach] != '\n'))
                ++reach;
            for (size_t end = reach; ! ok && end >= at + (isSlot ? 1 : 0) && end <= reach; --end)
            {
                ok = from (piece + 1, end);
                if (end == 0)
                    break;
            }
        }
        known = ok ? 1 : 0;
        return ok;
    };
    return from (0, 0);
}
}

std::string collapse (const std::string& address)
{
    std::string out;
    out.reserve (address.size());
    const size_t n = address.size();
    size_t i = 0;
    while (i < n)
    {
        if (isDigit (address[i]) && i > 0 && address[i - 1] == '/')
        {
            size_t end = i;
            while (end < n && isDigit (address[end]))
                ++end;
            // `$` also stands before a final newline.
            const bool segmentEnds = end == n || address[end] == '/' || (address[end] == '\n' && end + 1 == n);
            if (segmentEnds)
            {
                out += '0';
                i = end;
                continue;
            }
        }
        out += address[i++];
    }
    return out;
}

Counts countsOf (const std::vector<std::pair<std::string, long long>>& addressesAndCounts)
{
    Counts counts;
    for (const auto& [address, count] : addressesAndCounts)
        counts[collapse (address)] += count;
    return counts;
}

std::function<bool (const std::string&)> matcher (const std::string& pattern)
{
    const auto pieces = std::make_shared<const std::vector<Piece>> (piecesOf (pattern));
    return [pieces] (const std::string& address) {
        if (covers (*pieces, address, address.size()))
            return true;
        // `$` also matches before a final newline.
        return ! address.empty() && address.back() == '\n' && covers (*pieces, address, address.size() - 1);
    };
}

juce::var load (const juce::File& file)
{
    if (! file.existsAsFile())
        return emptyRegister ("[Errno 2] No such file or directory: '" + file.getFullPathName() + "'");

    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        return emptyRegister ("cannot read " + file.getFullPathName());
    const auto* chars = static_cast<const char*> (bytes.getData());
    const auto size = static_cast<int> (bytes.getSize());
    if (! juce::CharPointer_UTF8::isValidString (chars, size))
        return emptyRegister ("'utf-8' codec can't decode " + file.getFullPathName());

    juce::String error;
    const auto raw = parseJson (juce::String::fromUTF8 (chars, size), error);
    if (error.isNotEmpty())
        return emptyRegister (error);

    if (raw.getDynamicObject() == nullptr || raw["entries"].getArray() == nullptr)
        return emptyRegister (file.getFullPathName() + " is not a register");

    // What an entry carries. An older or newer file may be short of one, and a missing field is filled
    // in with a blank rather than taking the whole register down with it.
    static const char* const fields[] = { "address", "device", "direction", "source", "note" };
    juce::Array<juce::var> entries;
    for (const auto& item : *raw["entries"].getArray())
    {
        const auto* given = item.getDynamicObject();
        if (given == nullptr || ! given->hasProperty ("address") || ! pythonTruthy (item["address"]))
            continue;
        auto entry = object();
        for (const char* field : fields)
            entry.getDynamicObject()->setProperty (field, given->hasProperty (field) ? item[field] : juce::var (""));
        entries.add (entry);
    }

    auto loaded = object();
    auto* made = loaded.getDynamicObject();
    made->setProperty ("problem", "");
    made->setProperty ("devices", namesIn (raw["devices"]));
    made->setProperty ("directions", namesIn (raw["directions"]));
    made->setProperty ("entries", juce::var (entries));
    return loaded;
}

juce::var withCounts (const juce::var& registerData, const Counts& counts)
{
    // `seen` is false rather than absent when an address has never arrived -- the distinction the
    // traffic cannot make, because a row that was never created has no zero to show.
    juce::Array<juce::var> entries;
    int alive = 0;
    if (const auto* given = registerData["entries"].getArray())
        for (const auto& item : *given)
        {
            const auto match = matcher (item["address"].toString().toStdString());
            long long total = 0;
            for (const auto& [address, count] : counts)
                if (match (address))
                    total += count;
            auto entry = item.clone();
            entry.getDynamicObject()->setProperty ("count", static_cast<juce::int64> (total));
            entry.getDynamicObject()->setProperty ("seen", total > 0);
            alive += total > 0 ? 1 : 0;
            entries.add (entry);
        }

    const auto listOf = [&] (const char* key) {
        return registerData[key].getArray() != nullptr ? registerData[key].clone() : juce::var (juce::Array<juce::var>());
    };
    auto result = object();
    auto* made = result.getDynamicObject();
    made->setProperty ("problem", registerData.getDynamicObject() != nullptr && registerData.getDynamicObject()->hasProperty ("problem")
                                      ? registerData["problem"] : juce::var (""));
    made->setProperty ("devices", listOf ("devices"));
    made->setProperty ("directions", listOf ("directions"));
    made->setProperty ("entries", juce::var (entries));
    made->setProperty ("seen", alive);
    made->setProperty ("unseen", entries.size() - alive);
    return result;
}

juce::var registerAsJson (const juce::File& file, Traffic& traffic, double now)
{
    // Two tables are put in: the addresses Core understood and the ones it could not route (REAPER's
    // feedback filled the second until REAPER left the Core, 2026-10-27). The history is asked
    // for with a cutoff of now so it comes back empty: none of the ring is the catalogue's business.
    const auto snapshot = traffic.snapshot (now, now);
    const auto unknown = traffic.unknownSnapshot (now);
    std::vector<std::pair<std::string, long long>> rows;
    for (const auto& row : snapshot.rows)
        rows.emplace_back (row.address, row.count);
    for (const auto& row : unknown.rows)
        rows.emplace_back (row.address, row.count);
    return withCounts (load (file), countsOf (rows));
}
}
