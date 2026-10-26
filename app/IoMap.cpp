// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IoMap.h"
#include "JsonParse.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace a3app
{
namespace
{
using a3::jsonparse::Refusal;
using a3::jsonparse::refuse;
using a3::jsonparse::objectOf;

const juce::Array<juce::var>& listOf (const juce::var& v, const juce::String& what)
{
    const auto* list = v.getArray();
    if (list == nullptr)
        refuse (what + " must be a list");
    return *list;
}

std::string stringOf (const juce::var& v, const juce::String& what)
{
    if (! v.isString() || v.toString().isEmpty())
        refuse (what + " must be a non-empty string");
    return v.toString().toStdString();
}

void requireEnginePort (const std::set<std::string>& known, const std::string& name, const juce::String& where)
{
    if (known.count (name) == 0)
        refuse (where + " names the unknown engine port '" + juce::String (name) + "'");
}

IoMap parse (const juce::var& section, const PortNames& ports)
{
    std::set<std::string> known (ports.inputs.begin(), ports.inputs.end());
    known.insert (ports.outputs.begin(), ports.outputs.end());

    const auto& object = objectOf (section, "io");
    for (const auto& property : object.getProperties())
    {
        const auto key = property.name.toString();
        if (key != "jack" && key != "directLinks" && key != "deviceChannels")
            refuse ("unknown key '" + key + "' in io");
    }

    IoMap map;
    if (object.hasProperty ("jack"))
        for (const auto& property : objectOf (object.getProperty ("jack"), "io.jack").getProperties())
        {
            const auto port = property.name.toString().toStdString();
            requireEnginePort (known, port, "io.jack");
            for (const auto& peer : listOf (property.value, "io.jack." + property.name.toString()))
                map.jack[port].push_back (stringOf (peer, "io.jack." + property.name.toString() + " peer"));
        }
    if (object.hasProperty ("directLinks"))
        for (const auto& pair : listOf (object.getProperty ("directLinks"), "io.directLinks"))
        {
            const auto& two = listOf (pair, "io.directLinks entry");
            if (two.size() != 2)
                refuse ("io.directLinks entry must be [from, to]");
            map.directLinks.emplace_back (stringOf (two[0], "io.directLinks from"), stringOf (two[1], "io.directLinks to"));
        }
    if (object.hasProperty ("deviceChannels"))
        for (const auto& property : objectOf (object.getProperty ("deviceChannels"), "io.deviceChannels").getProperties())
        {
            const auto port = property.name.toString().toStdString();
            requireEnginePort (known, port, "io.deviceChannels");
            const auto& v = property.value;
            const bool whole = (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite ((double) v)
                            && (double) v == std::floor ((double) v) && (double) v >= 0 && (double) v < 1.0e6;
            if (! whole)
                refuse ("io.deviceChannels." + property.name.toString() + " must be a whole number >= 0");
            map.deviceChannels[port] = (int) v;
        }
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
    auto* jack = new juce::DynamicObject();
    for (const auto& [port, peers] : map.jack)
    {
        juce::Array<juce::var> list;
        for (const auto& peer : peers)
            list.add (juce::String (peer));
        jack->setProperty (juce::String (port), list);
    }
    juce::Array<juce::var> links;
    for (const auto& [from, to] : map.directLinks)
        links.add (juce::Array<juce::var> { juce::String (from), juce::String (to) });
    auto* channels = new juce::DynamicObject();
    for (const auto& [port, channel] : map.deviceChannels)
        channels->setProperty (juce::String (port), channel);

    auto* root = new juce::DynamicObject();
    root->setProperty ("jack", juce::var (jack));
    root->setProperty ("directLinks", links);
    root->setProperty ("deviceChannels", juce::var (channels));
    return juce::var (root);
}
}
