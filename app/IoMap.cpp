// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IoMap.h"
#include "JsonParse.h"
#include <cmath>
#include <set>

namespace a3app
{
namespace
{
using a3::jsonparse::Refusal;
using a3::jsonparse::refuse;
using a3::jsonparse::objectOf;

void parseDeviceChannels (const juce::DynamicObject& object, const PortNames& ports, IoMap& map)
{
    std::set<std::string> known (ports.inputs.begin(), ports.inputs.end());
    known.insert (ports.outputs.begin(), ports.outputs.end());
    for (const auto& property : objectOf (object.getProperty ("deviceChannels"), "io.deviceChannels").getProperties())
    {
        const auto port = property.name.toString().toStdString();
        if (known.count (port) == 0)
            refuse ("io.deviceChannels names the unknown engine port '" + juce::String (port) + "'");
        const auto& v = property.value;
        const bool whole = (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite ((double) v)
                        && (double) v == std::floor ((double) v) && (double) v >= 0 && (double) v < 1.0e6;
        if (! whole)
            refuse ("io.deviceChannels." + property.name.toString() + " must be a whole number >= 0");
        map.deviceChannels[port] = (int) v;
    }
}

int loopChannel (const juce::DynamicObject& loop, const char* key, bool required, int fallback)
{
    if (! loop.hasProperty (key))
    {
        if (required)
            refuse ("io.roundTripLoop needs \"" + juce::String (key) + "\"");
        return fallback;
    }
    const auto& v = loop.getProperty (key);
    const bool whole = (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite ((double) v)
                    && (double) v == std::floor ((double) v) && (double) v >= 1 && (double) v <= 1024;
    if (! whole)
        refuse ("io.roundTripLoop." + juce::String (key) + " must be a whole number from 1");
    return (int) v;
}

// Absent: the rig's default loop. null: none.
std::optional<RoundTripLoop> parseLoop (const juce::DynamicObject& object)
{
    if (! object.hasProperty ("roundTripLoop"))
        return RoundTripLoop {};
    const auto& value = object.getProperty ("roundTripLoop");
    if (value.isVoid())
        return std::nullopt;
    const auto& loop = objectOf (value, "io.roundTripLoop");
    a3::jsonparse::rejectUnknownKeys (loop, { "output", "input", "channels" }, "io.roundTripLoop");
    RoundTripLoop parsed;
    parsed.output = loopChannel (loop, "output", true, 0);
    parsed.input = loopChannel (loop, "input", true, 0);
    parsed.channels = loopChannel (loop, "channels", false, 1);
    return parsed;
}

// A whole number in [low, high], or a refusal naming io.analogLoop.<key>.
double analogNumber (const juce::DynamicObject& loop, const char* key, double low, double high, bool whole)
{
    const auto& v = loop.getProperty (key);
    const bool number = (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite ((double) v);
    if (! number || (double) v < low || (double) v > high || (whole && (double) v != std::floor ((double) v)))
        refuse ("io.analogLoop." + juce::String (key) + " must be a number in " + juce::String (low) + ".." + juce::String (high));
    return (double) v;
}

std::optional<AnalogLoop> parseAnalogLoop (const juce::DynamicObject& object)
{
    if (! object.hasProperty ("analogLoop"))
        return std::nullopt;
    const auto& loop = objectOf (object.getProperty ("analogLoop"), "io.analogLoop");
    a3::jsonparse::rejectUnknownKeys (loop, { "output", "input", "converterSamples", "sampleRate", "measuredOn" }, "io.analogLoop");
    for (const auto* key : { "output", "input" })
        if (! loop.hasProperty (key))
            refuse ("io.analogLoop needs \"" + juce::String (key) + "\" (0: not chosen)");
    AnalogLoop parsed;
    parsed.output = (int) analogNumber (loop, "output", 0, 1024, true);
    parsed.input = (int) analogNumber (loop, "input", 0, 1024, true);
    const int shareKeys = (int) loop.hasProperty ("converterSamples") + (int) loop.hasProperty ("sampleRate") + (int) loop.hasProperty ("measuredOn");
    if (shareKeys == 0)
        return parsed;
    if (shareKeys != 3 || ! loop.getProperty ("measuredOn").isString())
        refuse ("io.analogLoop: converterSamples, sampleRate and measuredOn (a date) come together");
    ConverterShare share;
    share.samples = (int) analogNumber (loop, "converterSamples", -1.0e5, 1.0e5, true);
    share.sampleRate = analogNumber (loop, "sampleRate", 1, 1.0e6, false);
    share.measuredOn = loop.getProperty ("measuredOn").toString();
    parsed.converters = share;
    return parsed;
}

// The library knows jack and directLinks; deviceChannels and roundTripLoop are ours, so the library sees a copy without them.
juce::var withoutOurKeys (const juce::DynamicObject& object)
{
    auto* copy = object.clone().release();
    copy->removeProperty ("deviceChannels");
    copy->removeProperty ("roundTripLoop");
    copy->removeProperty ("analogLoop");
    return juce::var (copy);
}

IoMap parse (const juce::var& section, const PortNames& ports)
{
    const auto& object = objectOf (section, "io");
    a3::jsonparse::rejectUnknownKeys (object, { "jack", "directLinks", "deviceChannels", "roundTripLoop", "analogLoop" }, "io");

    juce::String error;
    const auto links = a3::links::parseLinkMap (withoutOurKeys (object), ports.inputs, ports.outputs, error);
    if (! links)
        refuse (error);

    IoMap map;
    static_cast<a3::links::LinkMap&> (map) = *links;
    if (object.hasProperty ("deviceChannels"))
        parseDeviceChannels (object, ports, map);
    map.roundTripLoop = parseLoop (object);
    map.analogLoop = parseAnalogLoop (object);
    return map;
}
}

std::optional<IoMap> parseIoMap (const juce::var& section, const PortNames& ports, juce::String& error)
{
    try
    {
        error = {};
        return parse (section, ports);
    }
    catch (const Refusal& refusal)
    {
        error = refusal.reason;
        return std::nullopt;
    }
}

juce::var toJson (const IoMap& map)
{
    auto root = a3::links::toJson (map);
    auto* channels = new juce::DynamicObject();
    for (const auto& [port, channel] : map.deviceChannels)
        channels->setProperty (juce::String (port), channel);
    root.getDynamicObject()->setProperty ("deviceChannels", juce::var (channels));
    juce::var loop;   // null: no loop
    if (map.roundTripLoop)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("output", map.roundTripLoop->output);
        object->setProperty ("input", map.roundTripLoop->input);
        object->setProperty ("channels", map.roundTripLoop->channels);
        loop = juce::var (object);
    }
    root.getDynamicObject()->setProperty ("roundTripLoop", loop);
    if (map.analogLoop)
    {
        auto* analog = new juce::DynamicObject();
        analog->setProperty ("output", map.analogLoop->output);
        analog->setProperty ("input", map.analogLoop->input);
        if (const auto& share = map.analogLoop->converters)
        {
            analog->setProperty ("converterSamples", share->samples);
            analog->setProperty ("sampleRate", share->sampleRate);
            analog->setProperty ("measuredOn", share->measuredOn);
        }
        root.getDynamicObject()->setProperty ("analogLoop", juce::var (analog));
    }
    return root;
}

namespace
{
// The engine ports (or direct links) the map routes to device channel `channel` (1-based) on one side.
juce::StringArray routesOn (const IoMap& map, bool input, int channel)
{
    juce::StringArray users;
    const auto isThat = [&] (const std::string& peer)
    {
        const auto named = deviceChannelOfPeer (peer);
        return named && named->capture == input && named->channel == channel;
    };
    for (const auto& [port, peers] : map.jack)
        for (const auto& peer : peers)
            if (isThat (peer))
                users.addIfNotAlreadyThere (juce::String (port));
    for (const auto& [port, mapped] : map.deviceChannels)
        if (isEngineInputPort (port) == input && mapped + 1 == channel)
            users.addIfNotAlreadyThere (juce::String (port));
    for (const auto& [from, to] : map.directLinks)
        if (isThat (from) || isThat (to))
            users.addIfNotAlreadyThere (juce::String (from) + " -> " + juce::String (to));
    return users;
}
}

juce::String analogLoopRefusal (const IoMap& map)
{
    const auto& loop = map.analogLoop;
    if (! loop || (loop->output == 0 && loop->input == 0))
        return "no analog test pair chosen (I/O page)";
    if (loop->output == 0)
        return "no analog test output chosen (I/O page)";
    if (loop->input == 0)
        return "no analog test input chosen (I/O page)";
    for (const bool input : { false, true })
    {
        const int channel = input ? loop->input : loop->output;
        const juce::String side = juce::String (input ? "input " : "output ") + juce::String (channel);
        if (loopHolds (map.roundTripLoop, input, channel))
            return side + " is the digital loop's";
        if (const auto users = routesOn (map, input, channel); ! users.isEmpty())
            return side + " is in use (" + users.joinIntoString (", ") + ")";
    }
    return {};
}

std::vector<std::string> dropLoopRoutes (IoMap& map)
{
    std::vector<std::string> dropped;
    const auto& loop = map.roundTripLoop;
    if (! loop)
        return dropped;
    for (auto entry = map.jack.begin(); entry != map.jack.end();)
    {
        auto& peers = entry->second;
        const auto before = peers.size();
        for (auto peer = peers.begin(); peer != peers.end();)
        {
            if (! isLoopPeer (loop, *peer))
            {
                ++peer;
                continue;
            }
            dropped.push_back (entry->first + " -> " + *peer);
            peer = peers.erase (peer);
        }
        const bool emptiedByUs = peers.empty() && before > 0;   // an entry that was empty in the file stays
        entry = emptiedByUs ? map.jack.erase (entry) : std::next (entry);
    }
    for (auto entry = map.deviceChannels.begin(); entry != map.deviceChannels.end();)
    {
        const int channel = entry->second + 1;
        if (! loopHolds (loop, isEngineInputPort (entry->first), channel))
        {
            ++entry;
            continue;
        }
        dropped.push_back (entry->first + " -> device channel " + std::to_string (channel));
        entry = map.deviceChannels.erase (entry);
    }
    auto& links = map.directLinks;
    for (auto link = links.begin(); link != links.end();)
    {
        if (! isLoopPeer (loop, link->first) && ! isLoopPeer (loop, link->second))
        {
            ++link;
            continue;
        }
        dropped.push_back (link->first + " -> " + link->second);
        link = links.erase (link);
    }
    return dropped;
}
}
