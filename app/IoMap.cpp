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

// The library knows jack and directLinks; deviceChannels is ours, so the library sees a copy without it.
juce::var withoutDeviceChannels (const juce::DynamicObject& object)
{
    auto* copy = object.clone().release();
    copy->removeProperty ("deviceChannels");
    return juce::var (copy);
}

IoMap parse (const juce::var& section, const PortNames& ports)
{
    const auto& object = objectOf (section, "io");
    a3::jsonparse::rejectUnknownKeys (object, { "jack", "directLinks", "deviceChannels" }, "io");

    juce::String error;
    const auto links = a3::links::parseLinkMap (withoutDeviceChannels (object), ports.inputs, ports.outputs, error);
    if (! links)
        refuse (error);

    IoMap map;
    static_cast<a3::links::LinkMap&> (map) = *links;
    if (object.hasProperty ("deviceChannels"))
        parseDeviceChannels (object, ports, map);
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
    return root;
}
}
