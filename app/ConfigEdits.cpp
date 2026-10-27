// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ConfigEdits.h"
#include "SpatialConfig.h"
#include <algorithm>
#include <string>
#include <cmath>
#include <set>

namespace a3app
{
namespace
{
std::string shortName (const std::string& full)
{
    const auto colon = full.find (':');
    return colon == std::string::npos ? full : full.substr (colon + 1);
}

bool contains (const std::vector<std::string>& list, const std::string& value)
{
    return std::find (list.begin(), list.end(), value) != list.end();
}

// The next analog pair no channel uses, below the return; 0 when none is left.
int nextFreePair (const a3::EngineConfig& engine, const std::vector<int>& pairs)
{
    for (int pair = 1; pair * 2 <= engine.analogInputs; ++pair)
        if (pair != engine.returnAnalogPair && std::find (pairs.begin(), pairs.end(), pair) == pairs.end())
            return pair;
    return 0;
}

std::optional<AppConfig> pruned (AppConfig config, juce::String& error)
{
    const auto ports = portNamesFor (config, error);
    if (! ports)
        return std::nullopt;
    config.io = prunedTo (config.io, *ports);
    return config;
}
}

std::optional<PortNames> portNamesFor (const AppConfig& config, juce::String& error)
{
    const auto spatial = a3::loadSpatialConfig (config.spatialDirectory, error);
    if (! spatial)
    {
        error = "spatial (" + config.spatialDirectory.getFullPathName() + "): " + error;
        return std::nullopt;
    }
    error = {};
    return enginePortNames (config.engine, *spatial);
}

IoMap prunedTo (const IoMap& map, const PortNames& ports)
{
    std::set<std::string> known (ports.inputs.begin(), ports.inputs.end());
    known.insert (ports.outputs.begin(), ports.outputs.end());
    IoMap result;
    result.directLinks = map.directLinks;
    for (const auto& [port, peers] : map.jack)
        if (known.count (port) != 0)
            result.jack[port] = peers;
    for (const auto& [port, channel] : map.deviceChannels)
        if (known.count (port) != 0)
            result.deviceChannels[port] = channel;
    return result;
}

std::optional<AppConfig> withChannels (const AppConfig& config, int channels, juce::String& error)
{
    if (channels < 1 || channels > maxChannelsOnPage)
    {
        error = "channels must be within 1.." + juce::String (maxChannelsOnPage);
        return std::nullopt;
    }
    auto edited = config;
    edited.engine.channels = channels;
    auto& pairs = edited.engine.analogPairs;
    pairs.resize ((size_t) channels, -1);
    for (auto& pair : pairs)
        if (pair < 0)
            pair = nextFreePair (edited.engine, pairs);
    return pruned (std::move (edited), error);
}

std::optional<AppConfig> withSpatialDirectory (const AppConfig& config, const juce::File& directory, juce::String& error)
{
    auto edited = config;
    edited.spatialDirectory = directory;
    return pruned (std::move (edited), error);
}

std::optional<AppConfig> withTrim (const AppConfig& config, TrimZone zone, float db, juce::String& error)
{
    if (! std::isfinite (db) || db < minTrimDb || db > maxTrimDb)
    {
        error = "a trim must be within " + juce::String (minTrimDb, 0) + " and +" + juce::String (maxTrimDb, 0) + " dB";
        return std::nullopt;
    }
    error = {};
    auto edited = config;
    auto& trims = edited.engine.trims;
    switch (zone)
    {
        case TrimZone::main: trims.main = db; break;
        case TrimZone::booth: trims.booth = db; break;
        case TrimZone::phones: trims.phones = db; break;
        case TrimZone::rec: trims.rec = db; break;
    }
    return edited;
}

std::optional<AppConfig> withHeadphoneEq (const AppConfig& config, const juce::String& model, juce::String& error)
{
    if (! a3::headphoneEqChoice (model))
    {
        error = "unknown headphone model '" + model + "'";
        return std::nullopt;
    }
    error = {};
    auto edited = config;
    edited.engine.phonesHeadphoneEq = model;
    return edited;
}

namespace
{
struct DeckBank { const char* inputInfix; const char* localPort; const char* aes67Port; int count; };
constexpr DeckBank deckBanks[] = {
    { "_",     "bformat_", "receive_MAIN", deckInputChannels },
    { "_aux_", "aux_",     "receive_AUX",  deckAuxChannels },
    { "_cue_", "cue_",     "receive_CUE",  deckCueChannels },
};
}

void setDeckSource (IoMap& map, int slot, DeckSource source)
{
    const auto letter = deckLetters (slot);
    const auto client = source == DeckSource::local ? "StemDeck-" + letter : "AES67 deck " + letter;
    for (const auto& bank : deckBanks)
        for (int i = 1; i <= bank.count; ++i)
        {
            const auto n = std::to_string (i);
            const auto peerPort = std::string (source == DeckSource::local ? bank.localPort : bank.aes67Port) + n;
            map.jack["deck" + letter + bank.inputInfix + n] = { client + ":" + peerPort };
        }
}

DeckSource deckSourceOf (const IoMap& map, int slot)
{
    const auto link = map.jack.find ("deck" + deckLetters (slot) + "_1");
    if (link == map.jack.end() || link->second.empty())
        return DeckSource::local;
    return link->second.front().rfind ("AES67 deck ", 0) == 0 ? DeckSource::aes67 : DeckSource::local;
}

bool sameConfig (const AppConfig& a, const AppConfig& b)
{
    const auto& x = a.backend;
    const auto& y = b.backend;
    return a.engine == b.engine && a.spatialDirectory == b.spatialDirectory
        && x.type == y.type && x.deviceType == y.deviceType && x.inputDevice == y.inputDevice
        && x.outputDevice == y.outputDevice && x.sampleRate == y.sampleRate && x.blockSize == y.blockSize
        && a.io.jack == b.io.jack && a.io.directLinks == b.io.directLinks && a.io.deviceChannels == b.io.deviceChannels;
}

std::vector<IoRow> ioRows (const AppConfig& config, const PortNames& ports, const LiveLinks& links)
{
    std::vector<IoRow> rows;
    const auto add = [&] (const std::string& port, bool input)
    {
        IoRow row;
        row.port = port;
        row.input = input;
        if (const auto mapped = config.io.jack.find (port); mapped != config.io.jack.end())
            row.mapped = mapped->second;
        if (const auto channel = config.io.deviceChannels.find (port); channel != config.io.deviceChannels.end())
            row.deviceChannel = channel->second;
        for (const auto& [from, to] : links)
        {
            const auto& peer = input ? from : to;
            const auto& own = input ? to : from;
            if (shortName (own) == port && ! contains (row.mapped, peer) && ! contains (row.foreign, peer))
                row.foreign.push_back (peer);
        }
        rows.push_back (std::move (row));
    };
    for (const auto& port : ports.inputs)
        add (port, true);
    for (const auto& port : ports.outputs)
        add (port, false);
    return rows;
}
}
