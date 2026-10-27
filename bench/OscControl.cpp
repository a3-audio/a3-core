// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OscControl.h"
#include <algorithm>
#include <cmath>

namespace a3bench
{
namespace
{
struct Key { const char* name; std::atomic<float> a3::StripControls::* member; };
const Key controlKeys[] {
    { "channel.gain", &a3::StripControls::gain },
    { "channel.volume", &a3::StripControls::volume },
    { "channel.eq.high", &a3::StripControls::eqHigh },
    { "channel.eq.mid", &a3::StripControls::eqMid },
    { "channel.eq.low", &a3::StripControls::eqLow },
    { "channel.filter.frequency", &a3::StripControls::frequency },
    { "channel.filter.q", &a3::StripControls::q },
    { "channel.3d", &a3::StripControls::threeD },
};

std::optional<ControlMap::Entry> entryFrom (const juce::var& address, std::atomic<float> a3::StripControls::* member)
{
    const auto pattern = channelPatternOf (address);
    if (! pattern)
        return std::nullopt;
    ControlMap::Entry e;
    static_cast<ChannelPattern&> (e) = *pattern;
    e.member = member;
    return e;
}

std::optional<PositionMap::Entry> positionEntryFrom (const juce::var& address, bool elevation)
{
    const auto pattern = channelPatternOf (address);
    if (! pattern)
        return std::nullopt;
    PositionMap::Entry e;
    static_cast<ChannelPattern&> (e) = *pattern;
    e.elevation = elevation;
    return e;
}

// 1..3 plain digits: "+1", " 1" and "" are not channel numbers.
bool channelNumber (const juce::String& text, int& out)
{
    if (text.isEmpty() || text.length() > 3 || ! text.containsOnly ("0123456789"))
        return false;
    out = text.getIntValue();
    return true;
}
}

std::optional<ChannelPattern> channelPatternOf (const juce::var& address)
{
    const auto pattern = address["pattern"].toString();
    const auto split = pattern.indexOf ("{ch}");
    const auto range = address["ch"];
    if (split < 0 || ! range.isArray() || range.size() != 2)
        return std::nullopt;
    ChannelPattern p;
    p.prefix = pattern.substring (0, split);
    p.suffix = pattern.substring (split + 4);
    p.firstChannel = (int) range[0];
    p.lastChannel = (int) range[1];
    return p;
}

int channelOfAddress (const ChannelPattern& p, const juce::String& address, int numChannels)
{
    if (! address.startsWith (p.prefix) || ! address.endsWith (p.suffix)
        || address.length() <= p.prefix.length() + p.suffix.length())
        return 0;
    int channel = 0;
    const auto middle = address.substring (p.prefix.length(), address.length() - p.suffix.length());
    if (! channelNumber (middle, channel) || channel < p.firstChannel || channel > p.lastChannel
        || channel < 1 || channel > numChannels)
        return 0;
    return channel;
}

int listenerPort (const juce::var& root, const juce::String& name)
{
    const auto listeners = root["listeners"];
    for (int i = 0; i < listeners.size(); ++i)
        if (listeners[i]["name"].toString() == name)
            return (int) listeners[i]["port"];
    return -1;
}

juce::String listenerHostAddress (const juce::var& root, const juce::String& name)
{
    const auto listeners = root["listeners"];
    for (int i = 0; i < listeners.size(); ++i)
        if (listeners[i]["name"].toString() == name)
            return root["hosts"][juce::Identifier (listeners[i]["host"].toString())].toString();
    return {};
}

juce::String Truth::listenerOnPort (int port) const
{
    juce::StringArray names;
    for (const auto& l : listeners)
        if (l.second == port)
            names.add (l.first);
    return names.joinIntoString (", ");
}

bool parseMetersTarget (const std::string& argument, MetersTarget& out)
{
    const std::string prefix = "--meters-osc=";
    if (argument.rfind (prefix, 0) != 0)
        return false;
    const auto target = argument.substr (prefix.size());
    const auto colon = target.rfind (':');
    if (colon == std::string::npos || colon == 0)
        return false;
    const juce::String portText (target.substr (colon + 1));
    if (portText.isEmpty() || portText.length() > 5 || ! portText.containsOnly ("0123456789"))
        return false;
    const int port = portText.getIntValue();
    if (port < 1 || port > 65535)
        return false;
    out = { juce::String (target.substr (0, colon)), port };
    return true;
}

juce::String Truth::vuAddressFor (const juce::String& meterName) const
{
    for (size_t i = 0; i < vuMeters.size(); ++i)
        if (vuMeters[i] == meterName)
            return vuPattern.replace ("{n}", juce::String ((int) i + 1));
    return {};
}

std::optional<Truth> parseTruth (const juce::String& jsonText)
{
    const auto root = juce::JSON::parse (jsonText);
    const auto addresses = root["addresses"];
    if (! root.isObject() || ! addresses.isObject())
        return std::nullopt;

    Truth truth;
    for (const auto& key : controlKeys)
    {
        auto entry = entryFrom (addresses[juce::Identifier (key.name)], key.member);
        if (! entry)
            return std::nullopt;
        truth.controls.entries.push_back (*entry);
    }

    for (const bool elevation : { false, true })
    {
        auto entry = positionEntryFrom (addresses[elevation ? "channel.elevation" : "channel.azimuth"], elevation);
        if (! entry)
            return std::nullopt;
        truth.positions.entries.push_back (*entry);
    }

    const auto all = root["listeners"];
    for (int i = 0; i < all.size(); ++i)
        truth.listeners.emplace_back (all[i]["name"].toString(), (int) all[i]["port"]);
    truth.enginePort = listenerPort (root, "engine.osc");
    truth.engineBindAddress = listenerHostAddress (root, "engine.osc");
    truth.vuPattern = addresses["vu"]["pattern"].toString();
    const auto meters = root["vu_meters"];
    for (int i = 0; i < meters.size(); ++i)
        truth.vuMeters.push_back (meters[i].toString());
    if (truth.enginePort <= 0 || truth.engineBindAddress.isEmpty() || truth.vuPattern.isEmpty())
        return std::nullopt;
    return truth;
}

bool applyChannelMessage (const ControlMap& map, const juce::String& address, float value,
                          std::array<a3::StripControls*, 4>& strips)
{
    if (! std::isfinite (value))
        return false;
    for (const auto& entry : map.entries)
    {
        const int channel = channelOfAddress (entry, address, (int) strips.size());
        if (channel == 0)
            continue;
        (strips[(size_t) channel - 1]->*entry.member).store (std::clamp (value, 0.0f, 1.0f));
        return true;
    }
    return false;
}

bool applyPositionMessage (const PositionMap& map, const juce::String& address, float value,
                           a3::SpatialControls& controls)
{
    if (! std::isfinite (value))
        return false;
    for (const auto& entry : map.entries)
    {
        const int channel = channelOfAddress (entry, address, a3::numSpatialStrips);
        if (channel == 0)
            continue;
        const float limit = entry.elevation ? 90.0f : 180.0f;
        auto& target = entry.elevation ? controls.elevation[channel - 1] : controls.azimuth[channel - 1];
        target.store (std::clamp (value, -limit, limit));
        return true;
    }
    return false;
}
}
