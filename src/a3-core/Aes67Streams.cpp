// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Aes67Streams.h"

#include <cmath>
#include <cstdio>
#include <optional>

namespace a3core
{
namespace
{
const char* const legacyKey = "legacy-out";
const char* const coreSender = "core";

// The shipped table's legacy row: what a truth without a table still sends.
juce::var builtInLegacyRow()
{
    auto* row = new juce::DynamicObject();
    row->setProperty ("key", legacyKey);
    row->setProperty ("session", "AES67 {host} out");
    row->setProperty ("sender", coreSender);
    row->setProperty ("group", "aes67-core");
    row->setProperty ("port", 5004);
    row->setProperty ("ptime", 0.5);
    row->setProperty ("source", "fixed");
    row->setProperty ("channels", 12);
    row->setProperty ("legacy", true);
    return juce::var (row);
}

std::string text (const juce::var& row, const char* property) { return row[property].toString().toStdString(); }

bool isDottedAddress (const std::string& group)
{
    const auto parts = juce::StringArray::fromTokens (juce::String (group), ".", "");
    if (parts.size() != 4)
        return false;
    for (const auto& part : parts)
        if (part.isEmpty() || ! part.containsOnly ("0123456789"))
            return false;
    return true;
}

// A row's group is an address as written or a hosts key (Truth::host throws for an unknown one).
std::string resolveGroup (const Truth& truth, const std::string& group)
{
    return isDottedAddress (group) ? group : truth.host (group);
}

std::string withHost (std::string session, const std::string& hostname)
{
    const std::string placeholder = "{host}";
    for (auto at = session.find (placeholder); at != std::string::npos; at = session.find (placeholder, at + hostname.size()))
        session.replace (at, placeholder.size(), hostname);
    return session;
}

void addZone (StreamSpec& spec, const std::string& zone, const a3::Layout& layout)
{
    for (const auto& channel : a3::zoneChannels (layout))
    {
        spec.channelNames.push_back (channel.name);
        spec.enginePorts.push_back (zone + "_" + channel.portSuffix);
    }
}

void addPair (StreamSpec& spec, const std::string& base)
{
    for (const char* side : { "L", "R" })
    {
        spec.channelNames.push_back (side);
        spec.enginePorts.push_back (base + "_" + side);
    }
}

void addFixed (StreamSpec& spec, int channels)
{
    for (int k = 1; k <= channels; ++k)
        spec.channelNames.push_back ("CH" + std::to_string (k));
}

void addChannels (StreamSpec& spec, const juce::var& row, const a3::SpatialConfig& spatial)
{
    const auto source = text (row, "source");
    if (source == "fixed")
        return addFixed (spec, static_cast<int> (row["channels"]));
    if (source == "zone:main")
        return addZone (spec, "main", spatial.main.layout);
    if (source == "zone:booth")
        return addZone (spec, "booth", spatial.booth.layout);
    if (source == "pair:rec")
        return addPair (spec, "rec");
    if (source == "pair:phones")
        return addPair (spec, "phones");
    throw TruthError { "aes67_streams: '" + spec.key + "' has an unknown source '" + source + "'" };
}

StreamSpec resolveRow (const juce::var& row, const Truth& truth, const a3::SpatialConfig& spatial, const std::string& hostname)
{
    StreamSpec spec;
    spec.key = text (row, "key");
    spec.session = withHost (text (row, "session"), hostname);
    spec.group = resolveGroup (truth, text (row, "group"));
    spec.port = static_cast<int> (row["port"]);
    spec.ptimeMs = static_cast<double> (row["ptime"]);
    spec.legacy = static_cast<bool> (row["legacy"]);
    addChannels (spec, row, spatial);
    return spec;
}

// One RTP packet a 1500-byte frame: L24 at 44.1 kHz, the 12-byte RTP header, 28 bytes of IP and UDP.
constexpr int packetLimitBytes = 1472;

int packetBytes (const StreamSpec& spec)
{
    const auto samples = static_cast<int> (std::lround (spec.ptimeMs * 44.1));
    return static_cast<int> (spec.channelNames.size()) * 3 * samples + 12;
}

std::optional<std::string> packetProblem (const StreamSpec& spec)
{
    const auto bytes = packetBytes (spec);
    if (bytes <= packetLimitBytes)
        return std::nullopt;
    char ptime[32];
    std::snprintf (ptime, sizeof (ptime), "%.6g", spec.ptimeMs);
    return "aes67_streams: '" + spec.key + "' refused: " + std::to_string (spec.channelNames.size()) + " channels at " + ptime
           + " ms are a " + std::to_string (bytes) + "-byte packet, above the " + std::to_string (packetLimitBytes) + "-byte limit";
}

void report (std::vector<std::string>* problems, const std::string& line)
{
    if (problems != nullptr)
        problems->push_back (line);
}

bool isNumber (const juce::var& value) { return value.isInt() || value.isInt64() || value.isDouble(); }

std::string refusedRow (const juce::var& row, const std::string& why) { return "aes67_streams: '" + text (row, "key") + "' refused: " + why; }

// Why a row cannot be sent as written (port, ptime, a fixed row's channels, a group), or nothing.
std::optional<std::string> rowProblem (const juce::var& row)
{
    const auto port = row["port"];
    if (! isNumber (port) || static_cast<int> (port) < 1 || static_cast<int> (port) > 65535)
        return refusedRow (row, "port '" + port.toString().toStdString() + "' is not 1-65535");
    if (! isNumber (row["ptime"]) || static_cast<double> (row["ptime"]) <= 0.0)
        return refusedRow (row, "ptime '" + row["ptime"].toString().toStdString() + "' is not above 0");
    if (text (row, "source") == "fixed" && (! isNumber (row["channels"]) || static_cast<int> (row["channels"]) <= 0))
        return refusedRow (row, "a fixed stream's channels '" + row["channels"].toString().toStdString() + "' are not above 0");
    if (text (row, "group").empty())
        return refusedRow (row, "no group");
    return std::nullopt;
}

// The legacy stream: the table's row when it is valid, resolves and fits one packet; else the built-in row (what
// every receiver of the rig already knows) and a line that says why. Throws TruthError only when the built-in one
// does not resolve either.
StreamSpec legacyStream (const juce::var& row, const Truth& truth, const a3::SpatialConfig& spatial, const std::string& hostname,
                         std::vector<std::string>* problems)
{
    std::string why;
    try
    {
        if (const auto invalid = rowProblem (row))
            why = *invalid;
        else
        {
            auto spec = resolveRow (row, truth, spatial, hostname);
            if (const auto tooBig = packetProblem (spec))
                why = *tooBig;
            else
                return spec;
        }
    }
    catch (const TruthError& problem)
    {
        why = refusedRow (row, problem.what);
    }
    report (problems, why + "; the built-in legacy stream is sent");
    return resolveRow (builtInLegacyRow(), truth, spatial, hostname);
}

bool isCoreRow (const juce::var& row) { return row["sender"].toString() == coreSender; }

bool isLegacyRow (const juce::var& row) { return row["key"].toString() == legacyKey; }

// The table's Core rows, the legacy one first (the built-in one when the table has none).
std::vector<juce::var> coreRows (const Truth& truth)
{
    const auto table = truth.streamTable();
    std::vector<juce::var> rows { builtInLegacyRow() };
    for (const auto& row : *table.getArray())
    {
        if (! isCoreRow (row))
            continue;
        if (isLegacyRow (row))
            rows.front() = row;
        else
            rows.push_back (row);
    }
    return rows;
}
}

std::vector<StreamSpec> resolveTxStreams (const Truth& truth, const a3::SpatialConfig& spatial, const std::string& hostname,
                                          std::vector<std::string>* problems)
{
    const auto rows = coreRows (truth);
    std::vector<StreamSpec> streams { legacyStream (rows.front(), truth, spatial, hostname, problems) };
    for (std::size_t k = 1; k < rows.size(); ++k)
    {
        if (const auto invalid = rowProblem (rows[k]))
        {
            report (problems, *invalid);
            continue;
        }
        auto spec = resolveRow (rows[k], truth, spatial, hostname);
        if (const auto tooBig = packetProblem (spec))
            report (problems, *tooBig);
        else
            streams.push_back (std::move (spec));
    }
    return streams;
}

std::vector<StreamSpec> legacyTxStreams (const Truth& truth, const std::string& hostname, std::vector<std::string>* problems)
{
    return { legacyStream (coreRows (truth).front(), truth, a3::SpatialConfig {}, hostname, problems) };
}

std::string streamsSignature (const std::vector<StreamSpec>& streams)
{
    std::string signature;
    for (const auto& spec : streams)
    {
        char ptime[32];
        std::snprintf (ptime, sizeof (ptime), "%.6g", spec.ptimeMs);
        signature += spec.session + '\t' + spec.group + '\t' + std::to_string (spec.port) + '\t' + ptime;
        for (const auto& name : spec.channelNames)
            signature += '\t' + name;
        signature += '\n';
    }
    return signature;
}

bool streamsChanged (const std::string& renderedSignature, const std::vector<StreamSpec>& streams)
{
    return renderedSignature != streamsSignature (streams);
}
}
