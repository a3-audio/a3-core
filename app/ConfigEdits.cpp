// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ConfigEdits.h"
#include "SpatialConfig.h"
#include <algorithm>
#include <string>
#include <cmath>
#include <regex>
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

// The next analog pair no channel uses; 0 when none is left.
int nextFreePair (const a3::EngineConfig& engine, const std::vector<int>& pairs)
{
    for (int pair = 1; pair * 2 <= engine.analogInputs; ++pair)
        if (std::find (pairs.begin(), pairs.end(), pair) == pairs.end())
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
    const auto spatial = loadZonesSpatial (config, error);
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

std::optional<AppConfig> withDecks (const AppConfig& config, int decks, juce::String& error)
{
    if (decks < 1 || decks > maxDecksOnPage)
    {
        error = "decks must be within 1.." + juce::String (maxDecksOnPage);
        return std::nullopt;
    }
    auto edited = config;
    edited.engine.decks = decks;
    edited.engine.decksNamed = true;
    for (int slot = config.engine.decks + 1; slot <= decks; ++slot)
        linkUnnamedSlotLocally (edited.io, slot);
    return pruned (std::move (edited), error);
}

std::optional<AppConfig> withSpatialDirectory (const AppConfig& config, const juce::File& directory, juce::String& error)
{
    auto edited = config;
    edited.spatialDirectory = directory;
    edited.boothSpatialDirectory = juce::File();
    return pruned (std::move (edited), error);
}

namespace
{
void setZoneDirectory (AppConfig& config, a3::SpeakerZone zone, const juce::File& directory)
{
    const auto booth = zoneSpatialDirectory (config, a3::SpeakerZone::booth);
    if (zone == a3::SpeakerZone::main)
        config.spatialDirectory = directory;
    const auto newBooth = zone == a3::SpeakerZone::booth ? directory : booth;
    config.boothSpatialDirectory = newBooth == config.spatialDirectory ? juce::File() : newBooth;
}
}

std::optional<AppConfig> withZoneSpatialDirectory (const AppConfig& config, a3::SpeakerZone zone, const juce::File& directory,
                                                   juce::String& error)
{
    auto edited = config;
    setZoneDirectory (edited, zone, directory);
    return pruned (std::move (edited), error);
}

namespace
{
const juce::String stagingName = ".staging";

// "club-20261001-120000" (or "...-2") -> "club": a variant of a variant is named after the same base.
juce::String variantBase (const juce::String& name)
{
    static const std::regex stamped ("^(.+)-[0-9]{8}-[0-9]{6}(-[0-9]+)?$");
    std::smatch match;
    const auto text = name.toStdString();
    return std::regex_match (text, match, stamped) ? juce::String (match[1].str()) : name;
}

// A spatial directory is flat: its own files, never its subdirectories (the user root holds the variants).
bool copyFiles (const juce::File& from, const juce::File& to)
{
    if (! from.isDirectory() || ! to.createDirectory())
        return false;
    for (const auto& file : from.findChildFiles (juce::File::findFiles, false))
        if (! file.copyFileTo (to.getChildFile (file.getFileName())))
            return false;
    return true;
}

juce::Result writeZoneFiles (const juce::File& directory, a3::SpeakerZone zone, const juce::String& decoderJson, const juce::var& parameters)
{
    const juce::String name = a3::zoneName (zone);
    const auto decoder = directory.getChildFile (name + ".decoder.json");
    const auto values = directory.getChildFile (name + ".parameters.json");
    if (! decoder.replaceWithText (decoderJson, false, false, nullptr))
        return juce::Result::fail ("cannot write " + decoder.getFullPathName());
    if (! values.replaceWithText (juce::JSON::toString (parameters) + "\n", false, false, "\n"))
        return juce::Result::fail ("cannot write " + values.getFullPathName());
    return juce::Result::ok();
}

// <zone>.zone.json's topDirections from the decoder's own layout, one per top in Routing order (the order the
// engine numbers the tops in); its stages stay. A decoder without a LoudspeakerLayout keeps the directions.
juce::Result writeTopDirections (const juce::File& directory, a3::SpeakerZone zone, const juce::String& decoderJson)
{
    const auto decoder = juce::JSON::parse (decoderJson);
    const auto speakers = decoder["LoudspeakerLayout"]["Loudspeakers"];
    const auto routing = decoder["Decoder"]["Routing"];
    if (! speakers.isArray() || ! routing.isArray())
        return juce::Result::ok();

    const juce::String name = a3::zoneName (zone);
    juce::Array<juce::var> directions;
    for (int top = 0; top < routing.size(); ++top)
    {
        const int channel = (int) routing[top];
        juce::var found;
        for (int i = 0; i < speakers.size() && found.isVoid(); ++i)
            if (! (bool) speakers[i]["IsImaginary"] && (int) speakers[i]["Channel"] == channel)
                found = juce::Array<juce::var> { speakers[i]["Azimuth"], speakers[i]["Elevation"] };
        if (found.isVoid())
            return juce::Result::fail (name + ".decoder.json: its LoudspeakerLayout has no real loudspeaker on channel "
                                       + juce::String (channel) + ", which its Routing uses");
        directions.add (found);
    }
    const auto zoneFile = directory.getChildFile (name + ".zone.json");
    auto zoneJson = juce::JSON::parse (zoneFile.loadFileAsString());
    if (! zoneJson.isObject())
        zoneJson = juce::var (new juce::DynamicObject());
    zoneJson.getDynamicObject()->setProperty ("topDirections", directions);
    if (! zoneFile.replaceWithText (juce::JSON::toString (zoneJson) + "\n", false, false, "\n"))
        return juce::Result::fail ("cannot write " + zoneFile.getFullPathName());
    return juce::Result::ok();
}
}

juce::File zoneVariantDirectory (const AppConfig& config, const juce::File& userRoot, juce::Time now, a3::SpeakerZone zone)
{
    const auto name = variantBase (zoneSpatialDirectory (config, zone).getFileName()) + "-" + now.formatted ("%Y%m%d-%H%M%S");
    auto variant = userRoot.getChildFile (name);
    for (int n = 2; variant.exists(); ++n)
        variant = userRoot.getChildFile (name + "-" + juce::String (n));
    return variant;
}

std::optional<AppConfig> withZoneDecoder (const AppConfig& config, a3::SpeakerZone zone, const juce::String& decoderJson,
                                          const juce::var& parameters, const juce::File& userRoot, juce::String& error,
                                          juce::Time now)
{
    const auto source = zoneSpatialDirectory (config, zone);
    const auto variant = zoneVariantDirectory (config, userRoot, now, zone);
    const auto stagingRoot = userRoot.getChildFile (stagingName);
    const auto staging = stagingRoot.getChildFile (variant.getFileName());
    staging.deleteRecursively();   // only ever a leftover of an interrupted save of this very name
    const auto discard = [&] (const juce::String& why) -> std::optional<AppConfig>
    {
        staging.deleteRecursively();
        if (stagingRoot.findChildFiles (juce::File::findFilesAndDirectories, false).isEmpty())
            stagingRoot.deleteFile();
        error = why;
        return std::nullopt;
    };

    if (! copyFiles (source, staging))
        return discard ("cannot copy " + source.getFullPathName() + " to " + staging.getFullPathName());
    if (const auto written = writeZoneFiles (staging, zone, decoderJson, parameters); written.failed())
        return discard (written.getErrorMessage());
    if (const auto directions = writeTopDirections (staging, zone, decoderJson); directions.failed())
        return discard (directions.getErrorMessage());
    juce::String refusal;
    auto edited = withZoneSpatialDirectory (config, zone, staging, refusal);   // loads it: the engine would refuse what fails here
    if (! edited)
        return discard (refusal);

    if (! staging.moveFileTo (variant))
        return discard ("cannot move " + staging.getFullPathName() + " to " + variant.getFullPathName());
    if (stagingRoot.findChildFiles (juce::File::findFilesAndDirectories, false).isEmpty())
        stagingRoot.deleteFile();
    setZoneDirectory (*edited, zone, variant);
    error = {};
    return edited;
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

std::vector<std::pair<std::string, std::string>> deckLinks (int slot, DeckSource source)
{
    std::vector<std::pair<std::string, std::string>> links;
    const auto letter = deckLetters (slot);
    const auto client = source == DeckSource::local ? "StemDeck-" + letter : "AES67 deck " + letter;
    for (const auto& bank : deckBanks)
        for (int i = 1; i <= bank.count; ++i)
        {
            const auto n = std::to_string (i);
            const auto peerPort = std::string (source == DeckSource::local ? bank.localPort : bank.aes67Port) + n;
            links.emplace_back ("deck" + letter + bank.inputInfix + n, client + ":" + peerPort);
        }
    return links;
}

void setDeckSource (IoMap& map, int slot, DeckSource source)
{
    for (const auto& [port, peer] : deckLinks (slot, source))
        map.jack[port] = { peer };
}

void linkUnnamedSlotLocally (IoMap& map, int slot)
{
    const auto links = deckLinks (slot, DeckSource::local);
    const bool named = std::any_of (links.begin(), links.end(), [&] (const auto& link)
    {
        return map.jack.count (link.first) > 0 || map.deviceChannels.count (link.first) > 0;
    });
    if (! named)
        setDeckSource (map, slot, DeckSource::local);
}

void linkUnnamedSlotsLocally (IoMap& map, int decks)
{
    for (int slot = 1; slot <= decks; ++slot)
        linkUnnamedSlotLocally (map, slot);
}

std::optional<DeckSource> deckSourceOf (const IoMap& map, int slot)
{
    for (const auto source : { DeckSource::local, DeckSource::aes67 })
    {
        const auto links = deckLinks (slot, source);
        const bool all = std::all_of (links.begin(), links.end(), [&] (const auto& link)
        {
            const auto found = map.jack.find (link.first);
            return found != map.jack.end() && found->second == std::vector<std::string> { link.second };
        });
        if (all)
            return source;
    }
    return std::nullopt;
}

std::vector<ZoneOutputs> zoneOutputGroups (const PortNames& ports)
{
    std::vector<ZoneOutputs> groups { { "MAIN", {} }, { "BOOTH", {} }, { "PHONES", {} }, { "REC", {} } };
    const std::pair<const char*, size_t> prefixes[] { { "main_", 0 }, { "booth_", 1 }, { "phones_", 2 }, { "rec_", 3 } };
    for (const auto& port : ports.outputs)
        for (const auto& [prefix, group] : prefixes)
            if (port.rfind (prefix, 0) == 0)
                groups[group].ports.push_back (port);
    return groups;
}

std::vector<ChannelInputs> channelInputPorts (const a3::EngineConfig& engine)
{
    std::vector<ChannelInputs> channels;
    for (int n = 1; n <= engine.channels; ++n)
    {
        ChannelInputs channel;
        channel.channel = n;
        const int pair = n <= (int) engine.analogPairs.size() ? engine.analogPairs[(size_t) n - 1] : 0;
        if (pair > 0)
        {
            channel.left = "analog_" + std::to_string (2 * pair - 1);
            channel.right = "analog_" + std::to_string (2 * pair);
        }
        channels.push_back (channel);
    }
    return channels;
}

AppConfig withJackRoute (const AppConfig& config, const std::string& port, const std::string& peer)
{
    auto edited = config;
    if (peer.empty())
        edited.io.jack.erase (port);
    else
        edited.io.jack[port] = { peer };
    return edited;
}

AppConfig withDeviceRoute (const AppConfig& config, const std::string& port, std::optional<int> channel)
{
    auto edited = config;
    if (channel)
        edited.io.deviceChannels[port] = *channel;
    else
        edited.io.deviceChannels.erase (port);
    return edited;
}

juce::String peerLabel (const std::string& port, const juce::String& deviceName)
{
    const auto named = deviceChannelOfPeer (port);
    if (! named)
        return juce::String (port);
    return (deviceName.isEmpty() ? juce::String ("Device") : deviceName) + (named->capture ? " in " : " out ") + juce::String (named->channel);
}

std::vector<RouteChoice> routeChoices (const AppConfig& config, const juce::StringArray& hardwarePorts, int deviceChannels, bool input,
                                       const juce::String& deviceName)
{
    std::vector<RouteChoice> choices;
    const auto& loop = config.io.roundTripLoop;
    if (config.backend.type == BackendSettings::Type::device)
    {
        for (int channel = 1; channel <= deviceChannels; ++channel)
            if (! loopHolds (loop, input, channel))
                choices.push_back ({ "channel " + juce::String (channel), {}, channel - 1 });
        return choices;
    }
    for (const auto& port : hardwarePorts)
        if (! isLoopPeer (loop, port.toStdString()))
            choices.push_back ({ peerLabel (port.toStdString(), deviceName), port.toStdString(), std::nullopt });
    return choices;
}

namespace
{
// The 1-based channels of one side: JACK: those of the hardware ports (RoundTripLoop.h deviceChannelOfPeer); a
// device: 1..deviceChannels. Sorted, each once.
std::vector<int> hardwareChannels (const AppConfig& config, const juce::StringArray& hardwarePorts, int deviceChannels, bool input)
{
    std::vector<int> channels;
    if (config.backend.type == BackendSettings::Type::device)
        for (int channel = 1; channel <= deviceChannels; ++channel)
            channels.push_back (channel);
    else
        for (const auto& port : hardwarePorts)
            if (const auto named = deviceChannelOfPeer (port.toStdString()); named && named->capture == input)
                channels.push_back (named->channel);
    std::sort (channels.begin(), channels.end());
    channels.erase (std::unique (channels.begin(), channels.end()), channels.end());
    return channels;
}

// Why the digital loop cannot reserve `loop`'s channel on one side ("" when it can).
juce::String digitalLoopRefusal (const IoMap& io, const RoundTripLoop& loop, bool input)
{
    const int channel = input ? loop.input : loop.output;
    const juce::String side = juce::String (input ? "input " : "output ") + juce::String (channel);
    if (const auto users = routesOn (io, input, channel); ! users.isEmpty())
        return side + " is in use (" + users.joinIntoString (", ") + "); unroute it first";
    if (io.analogLoop && (input ? io.analogLoop->input : io.analogLoop->output) == channel)
        return side + " is the analog test cable's";
    return {};
}
}

std::vector<int> analogTestChannels (const AppConfig& config, const juce::StringArray& hardwarePorts, int deviceChannels, bool input)
{
    auto channels = hardwareChannels (config, hardwarePorts, deviceChannels, input);
    channels.erase (std::remove_if (channels.begin(), channels.end(),
                                    [&] (int channel) { return loopHolds (config.io.roundTripLoop, input, channel); }),
                    channels.end());
    return channels;
}

std::vector<int> digitalLoopChannels (const AppConfig& config, const juce::StringArray& hardwarePorts, int deviceChannels, bool input)
{
    return hardwareChannels (config, hardwarePorts, deviceChannels, input);
}

std::optional<AppConfig> withDigitalLoopChannel (const AppConfig& config, bool input, int channel, juce::String& error)
{
    auto edited = config;
    if (channel <= 0)
    {
        edited.io.roundTripLoop.reset();
        return edited;
    }
    const bool fresh = ! config.io.roundTripLoop;
    auto loop = config.io.roundTripLoop.value_or (RoundTripLoop { channel, channel });
    (input ? loop.input : loop.output) = channel;
    for (const bool side : { false, true })
        if (fresh || side == input)
            if (const auto refusal = digitalLoopRefusal (config.io, loop, side); refusal.isNotEmpty())
            {
                error = "digital loop: " + refusal;
                return std::nullopt;
            }
    edited.io.roundTripLoop = loop;
    return edited;
}

AppConfig withAnalogTestChannel (const AppConfig& config, bool input, int channel)
{
    auto edited = config;
    auto loop = edited.io.analogLoop.value_or (AnalogLoop {});
    (input ? loop.input : loop.output) = std::max (0, channel);
    const bool empty = loop.output == 0 && loop.input == 0 && ! loop.converters;
    edited.io.analogLoop = empty ? std::nullopt : std::optional<AnalogLoop> (loop);
    return edited;
}

namespace
{
juce::String reservedText (const juce::String& what) { return what + " is the round-trip loop's (reserved)"; }
}

juce::String loopRefusal (const AppConfig& config, const std::vector<std::string>& peers)
{
    for (const auto& peer : peers)
        if (isLoopPeer (config.io.roundTripLoop, peer))
            return reservedText (juce::String (peer));
    return {};
}

juce::String loopRefusal (const AppConfig& config, bool input, int channel0)
{
    if (! loopHolds (config.io.roundTripLoop, input, channel0 + 1))
        return {};
    return reservedText ("device " + juce::String (input ? "input " : "output ") + juce::String (channel0 + 1));
}

juce::String loopNotice (const AppConfig& config, const std::vector<std::string>& loopDropped)
{
    const auto& loop = config.io.roundTripLoop;
    if (! loop)
        return {};
    auto notice = "Round-trip loop: device " + loopRangeText (*loop, false) + " -> " + loopRangeText (*loop, true)
                + " (reserved for the measurement, not offered here).";
    if (loopDropped.empty())
        return notice;
    juce::StringArray routes;
    for (const auto& route : loopDropped)
        routes.add (juce::String (route));
    return notice + " Unrouted at load, they were on the loop: " + routes.joinIntoString (", ") + ".";
}

std::vector<std::string> portsRoutedTo (const AppConfig& config, const std::string& peer, const std::string& except)
{
    std::vector<std::string> ports;
    if (peer.rfind ("#", 0) == 0)
    {
        const int channel = std::stoi (peer.substr (1));
        for (const auto& [port, mapped] : config.io.deviceChannels)
            if (port != except && mapped == channel)
                ports.push_back (port);
        return ports;
    }
    for (const auto& [port, peers] : config.io.jack)
        if (port != except && std::find (peers.begin(), peers.end(), peer) != peers.end())
            ports.push_back (port);
    return ports;
}

bool isUserVariant (const juce::File& directory, const juce::File& userRoot)
{
    // The parent must be the root itself, by path: a path holding ".." never matches, and a link could point anywhere.
    return userRoot != juce::File() && directory.getParentDirectory() == userRoot && ! directory.getFileName().startsWithChar ('.')
        && ! directory.getFileName().contains ("..") && directory.isDirectory() && ! directory.isSymbolicLink()
        && ! userRoot.isSymbolicLink();
}

bool usesVariant (const AppConfig& config, const juce::File& directory)
{
    return zoneSpatialDirectory (config, a3::SpeakerZone::main) == directory || zoneSpatialDirectory (config, a3::SpeakerZone::booth) == directory;
}

juce::String deleteRefusal (const juce::File& directory, const juce::File& userRoot, const AppConfig& running, const AppConfig& pending)
{
    if (! isUserVariant (directory, userRoot))
        return "installed or not one of your variants: only your own (" + userRoot.getFullPathName() + ") can be deleted";
    if (usesVariant (running, directory))
        return "what runs uses it";
    if (usesVariant (pending, directory))
        return "the pending edit uses it";
    return {};
}

juce::Result deleteVariant (const juce::File& directory, const juce::File& userRoot, const AppConfig& running, const AppConfig& pending)
{
    if (const auto why = deleteRefusal (directory, userRoot, running, pending); why.isNotEmpty())
        return juce::Result::fail (why);
    return directory.deleteRecursively (false) ? juce::Result::ok() : juce::Result::fail ("cannot delete " + directory.getFullPathName());
}

juce::String renameRefusal (const juce::File& directory, const juce::File& userRoot, const AppConfig& running)
{
    if (! isUserVariant (directory, userRoot))
        return "installed or not one of your variants: only your own can be renamed";
    if (usesVariant (running, directory))
        return "what runs uses it (its saved file names it): save another variant first";
    return {};
}

juce::String renameRefusal (const juce::File& directory, const juce::String& name, const juce::File& userRoot, const AppConfig& running)
{
    if (const auto why = renameRefusal (directory, userRoot, running); why.isNotEmpty())
        return why;
    if (name.trim().isEmpty())
        return "a name must not be blank";
    if (name.containsChar ('/') || name.containsChar ('\\'))
        return "a name must not hold a slash";
    if (name.startsWithChar ('.'))
        return "a name must not start with a dot";
    if (userRoot.getChildFile (name).exists())
        return name + " exists";
    return {};
}

std::optional<juce::File> renameVariant (const juce::File& directory, const juce::String& name, const juce::File& userRoot,
                                         const AppConfig& running, juce::String& error)
{
    error = renameRefusal (directory, name, userRoot, running);
    if (error.isNotEmpty())
        return std::nullopt;
    const auto target = userRoot.getChildFile (name);
    if (! directory.moveFileTo (target))
    {
        error = "cannot rename " + directory.getFullPathName() + " to " + name;
        return std::nullopt;
    }
    return target;
}

AppConfig withRenamedVariant (const AppConfig& config, const juce::File& from, const juce::File& to)
{
    auto edited = config;
    for (const auto zone : { a3::SpeakerZone::main, a3::SpeakerZone::booth })
        if (zoneSpatialDirectory (config, zone) == from)
            setZoneDirectory (edited, zone, to);
    return edited;
}

bool sameConfig (const AppConfig& a, const AppConfig& b)
{
    const auto& x = a.backend;
    const auto& y = b.backend;
    return a.engine == b.engine && a.spatialDirectory == b.spatialDirectory
        && zoneSpatialDirectory (a, a3::SpeakerZone::booth) == zoneSpatialDirectory (b, a3::SpeakerZone::booth)
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
