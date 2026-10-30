// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemDevice.h"
#include <algorithm>
#include <juce_core/juce_core.h>

namespace a3app
{
namespace
{
// The node name in a line "update: id:0 key:'<key>' value:'{"name":"<node>"}' ...", or "".
std::string nodeNameIn (const juce::String& line, const juce::String& key)
{
    if (! line.contains ("key:'" + key + "'"))
        return {};
    const auto value = line.fromFirstOccurrenceOf ("value:'", false, false).upToFirstOccurrenceOf ("'", false, false);
    return juce::JSON::parse (value)["name"].toString().toStdString();
}

bool startsWith (const std::string& text, const std::string& prefix) { return text.rfind (prefix, 0) == 0; }

bool sidePresent (const std::string& now, const std::string& configured)
{
    if (now.empty())
        return false;
    return configured.empty() ? startsWith (now, "alsa_") : now == configured;
}

bool absent (const std::string& peer, DevicePresence presence)
{
    return (! presence.playback && startsWith (peer, "system:playback_")) || (! presence.capture && startsWith (peer, "system:capture_"));
}

void addOnce (std::vector<std::string>& names, const std::string& name)
{
    if (std::find (names.begin(), names.end(), name) == names.end())
        names.push_back (name);
}
}

Command defaultNodesCommand() { return { "pw-metadata", "-n", "default", "0" }; }

DefaultNodes parseDefaultNodes (const std::string& output)
{
    DefaultNodes nodes;
    const std::pair<const char*, std::string*> keys[] = { { "default.audio.sink", &nodes.sink },
                                                           { "default.audio.source", &nodes.source },
                                                           { "default.configured.audio.sink", &nodes.configuredSink },
                                                           { "default.configured.audio.source", &nodes.configuredSource } };
    for (const auto& line : juce::StringArray::fromLines (juce::String (output)))
        for (const auto& [key, node] : keys)
            if (const auto name = nodeNameIn (line, key); ! name.empty())
                *node = name;
    return nodes;
}

DevicePresence devicePresenceOf (const DefaultNodes& nodes)
{
    return { sidePresent (nodes.sink, nodes.configuredSink), sidePresent (nodes.source, nodes.configuredSource) };
}

IoMap withoutAbsentDevice (const IoMap& io, DevicePresence presence, std::vector<std::string>& dropped)
{
    dropped.clear();
    IoMap kept = io;
    kept.jack.clear();
    kept.directLinks.clear();
    for (const auto& [port, peers] : io.jack)
        for (const auto& peer : peers)
        {
            if (absent (peer, presence))
                addOnce (dropped, peer);
            else
                kept.jack[port].push_back (peer);
        }
    for (const auto& link : io.directLinks)
    {
        if (absent (link.first, presence) || absent (link.second, presence))
        {
            for (const auto& end : { link.first, link.second })
                if (absent (end, presence))
                    addOnce (dropped, end);
            continue;
        }
        kept.directLinks.push_back (link);
    }
    return kept;
}

juce::String deviceNameOf (const std::string& alsaNode)
{
    // alsa_output.usb-<Vendor>_<Model words>_<serial>-<nn>.<profile>
    const juce::String node (alsaNode);
    const auto bus = node.fromFirstOccurrenceOf (".usb-", false, false).upToFirstOccurrenceOf ("-", false, false);
    auto words = juce::StringArray::fromTokens (bus, "_", "");
    if (words.size() < 3)
        return "Device";
    words.remove (0);                  // the vendor
    words.remove (words.size() - 1);   // the serial number
    return words.joinIntoString (" ");
}

juce::String deviceNameOf (const DefaultNodes& nodes)
{
    return deviceNameOf (! nodes.configuredSink.empty() ? nodes.configuredSink : nodes.sink);
}

SystemDeviceWatch::SystemDeviceWatch (CommandRunner& r) : runner (r) {}

DevicePresence SystemDeviceWatch::read()
{
    const auto listed = runner.run (defaultNodesCommand());
    if (listed.exitCode != 0)
        return last;
    lastNodes = parseDefaultNodes (listed.output);
    last = devicePresenceOf (lastNodes);
    return last;
}
}
