// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <juce_core/juce_core.h>
#include "ChannelStrip.h"
#include "SpatialBus.h"

namespace a3bench
{
// A "{ch}" address of the truth, split around {ch}, with the channel range the truth gives.
struct ChannelPattern
{
    juce::String prefix, suffix;   // "/channel/" and "/gain" for "/channel/{ch}/gain"
    int firstChannel = 1, lastChannel = 1;
};

// nullopt unless the truth entry has a pattern with "{ch}" and a two-number "ch" range.
std::optional<ChannelPattern> channelPatternOf (const juce::var& truthAddress);

// The channel (1-based) `address` names under the pattern, when it lies in the pattern's
// range and in 1..numChannels; 0 otherwise. "+1", " 1", "01x" and "" are not channel numbers.
int channelOfAddress (const ChannelPattern&, const juce::String& address, int numChannels);

// A listener of the parsed truth (its root object) by name: its port (-1 if absent) and
// its host's address through the truth's hosts ("" if absent).
int listenerPort (const juce::var& truthRoot, const juce::String& name);
juce::String listenerHostAddress (const juce::var& truthRoot, const juce::String& name);

// The channel controls the bench owns, read from the truth by name: one entry
// per key (channel.gain, channel.eq.high, ...) with the channel range the truth gives.
struct ControlMap
{
    struct Entry : ChannelPattern
    {
        std::atomic<float> a3::StripControls::* member = nullptr;
    };
    std::vector<Entry> entries;
};

// The channel positions (channel.azimuth / channel.elevation), degrees.
struct PositionMap
{
    struct Entry : ChannelPattern
    {
        bool elevation = false;
    };
    std::vector<Entry> entries;
};

// --meters-osc=HOST:PORT. There is no default target: the /vu/51-66 addresses are
// the analyzer's live ones, so the destination is always the caller's choice.
struct MetersTarget { juce::String host; int port = 0; };
bool parseMetersTarget (const std::string& argument, MetersTarget& out);

// What the bench takes from a3-osc.json.
struct Truth
{
    ControlMap controls;
    PositionMap positions;
    int enginePort = -1;                  // listener "engine.osc"
    juce::String engineBindAddress;       // its host name resolved through the truth's hosts ("local" is loopback)
    juce::String vuPattern;               // "/vu/{n}"
    std::vector<juce::String> vuMeters;   // meter n is the entry n-1

    std::vector<std::pair<juce::String, int>> listeners;   // every listener of the truth: name, port

    // The names of the listeners on that port, ", "-joined; "" if none.
    juce::String listenerOnPort (int port) const;
    juce::String vuAddressFor (const juce::String& meterName) const;   // "" if the truth has no such meter
};

// nullopt for text that is not the truth or lacks what the bench needs (the
// engine listener, any of the eight control keys, channel.azimuth or channel.elevation, the vu pattern).
std::optional<Truth> parseTruth (const juce::String& jsonText);

// "/channel/3/volume" 0.7 sets strip 2's volume; false (and nothing changed)
// for any address the map does not own, a channel outside the strips, or a
// non-finite value. Finite values are clamped to the wire's 0..1.
bool applyChannelMessage (const ControlMap& map, const juce::String& address, float value,
                          std::array<a3::StripControls*, 4>& strips);

// "/channel/2/azimuth" 200 sets azimuth[1] to 180: clamped to +-180 (azimuth) or
// +-90 (elevation) like Core. false (nothing changed) for a non-finite value, an
// address the map does not own, or a channel outside the truth's range or 1..4.
bool applyPositionMessage (const PositionMap&, const juce::String& address, float value, a3::SpatialControls&);
}
