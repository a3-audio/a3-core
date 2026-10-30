// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EnginePorts.h"
#include "RoundTripLoop.h"
#include "a3-links/LinkMap.h"
#include <juce_core/juce_core.h>
#include <map>
#include <string>

namespace a3app
{
// The library's link map plus what only the device backend uses.
struct IoMap : a3::links::LinkMap
{
    std::map<std::string, int> deviceChannels;                             // engine port -> device channel (0-based)
    std::optional<RoundTripLoop> roundTripLoop;                            // parseIoMap: the default when the section names none
    std::optional<AnalogLoop> analogLoop;                                  // none unless the section names one
};

juce::var toJson (const IoMap&);
// nullopt + error: names the entry that is wrong. Every engine-side name must be one of `ports`.
std::optional<IoMap> parseIoMap (const juce::var& section, const PortNames& ports, juce::String& error);

// Removes every route onto the round-trip loop's channels (a JACK peer, a device channel, a direct link with a
// loop end) and returns them, "<port> -> <peer>" / "<port> -> device channel <n>" (1-based) / "<from> -> <to>".
std::vector<std::string> dropLoopRoutes (IoMap&);

// Why the analog chain cannot be measured with this map ("" when it can): no pair chosen, a side on the digital
// loop, or a side some route of the map uses ("output 6 is in use (main_top_1)").
juce::String analogLoopRefusal (const IoMap&);
}
