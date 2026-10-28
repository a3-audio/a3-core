// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinkMap.h"
#include "../a3-engine/JsonParse.h"   // header-only, juce_core: shared with the config parsers
#include <set>

namespace a3::links
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

LinkMap parse (const juce::var& section, const std::vector<std::string>& ownInputs, const std::vector<std::string>& ownOutputs)
{
    std::set<std::string> known (ownInputs.begin(), ownInputs.end());
    known.insert (ownOutputs.begin(), ownOutputs.end());

    const auto& object = objectOf (section, "io");
    a3::jsonparse::rejectUnknownKeys (object, { "jack", "directLinks" }, "io");

    LinkMap map;
    if (object.hasProperty ("jack"))
        for (const auto& property : objectOf (object.getProperty ("jack"), "io.jack").getProperties())
        {
            const auto port = property.name.toString().toStdString();
            if (known.count (port) == 0)
                refuse ("io.jack names the unknown engine port '" + juce::String (port) + "'");
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
    return map;
}
}

std::optional<LinkMap> parseLinkMap (const juce::var& section, const std::vector<std::string>& ownInputs,
                                     const std::vector<std::string>& ownOutputs, juce::String& error)
{
    try
    {
        error = {};
        return parse (section, ownInputs, ownOutputs);
    }
    catch (const Refusal& refusal)
    {
        error = refusal.reason;
        return std::nullopt;
    }
}

juce::var toJson (const LinkMap& map)
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

    auto* root = new juce::DynamicObject();
    root->setProperty ("jack", juce::var (jack));
    root->setProperty ("directLinks", links);
    return juce::var (root);
}
}
