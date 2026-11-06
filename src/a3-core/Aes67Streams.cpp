// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Aes67Streams.h"

#include <cstdio>

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

std::vector<StreamSpec> resolveTxStreams (const Truth& truth, const a3::SpatialConfig& spatial, const std::string& hostname)
{
    std::vector<StreamSpec> streams;
    for (const auto& row : coreRows (truth))
        streams.push_back (resolveRow (row, truth, spatial, hostname));
    return streams;
}

std::vector<StreamSpec> legacyTxStreams (const Truth& truth, const std::string& hostname)
{
    return { resolveRow (coreRows (truth).front(), truth, a3::SpatialConfig {}, hostname) };
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
