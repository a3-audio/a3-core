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
    const auto pattern = address["pattern"].toString();
    const auto split = pattern.indexOf ("{ch}");
    const auto range = address["ch"];
    if (split < 0 || ! range.isArray() || range.size() != 2)
        return std::nullopt;
    ControlMap::Entry e;
    e.prefix = pattern.substring (0, split);
    e.suffix = pattern.substring (split + 4);
    e.firstChannel = (int) range[0];
    e.lastChannel = (int) range[1];
    e.member = member;
    return e;
}

int portOfListener (const juce::var& listeners, const juce::String& name)
{
    for (int i = 0; i < listeners.size(); ++i)
        if (listeners[i]["name"].toString() == name)
            return (int) listeners[i]["port"];
    return -1;
}

juce::String hostAddressOfListener (const juce::var& root, const juce::String& name)
{
    const auto listeners = root["listeners"];
    for (int i = 0; i < listeners.size(); ++i)
        if (listeners[i]["name"].toString() == name)
            return root["hosts"][juce::Identifier (listeners[i]["host"].toString())].toString();
    return {};
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

    truth.enginePort = portOfListener (root["listeners"], "engine.osc");
    truth.engineBindAddress = hostAddressOfListener (root, "engine.osc");
    truth.motionVuPort = portOfListener (root["listeners"], "motion.vu");
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
        if (! address.startsWith (entry.prefix) || ! address.endsWith (entry.suffix)
            || address.length() <= entry.prefix.length() + entry.suffix.length())
            continue;
        int channel = 0;
        const auto middle = address.substring (entry.prefix.length(), address.length() - entry.suffix.length());
        if (! channelNumber (middle, channel) || channel < entry.firstChannel || channel > entry.lastChannel
            || channel < 1 || channel > (int) strips.size())
            continue;
        (strips[(size_t) channel - 1]->*entry.member).store (std::clamp (value, 0.0f, 1.0f));
        return true;
    }
    return false;
}
}
