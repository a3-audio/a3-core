// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EnginePorts.h"
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
};

juce::var toJson (const IoMap&);
// nullopt + error: names the entry that is wrong. Every engine-side name must be one of `ports`.
std::optional<IoMap> parseIoMap (const juce::var& section, const PortNames& ports, juce::String& error);
}
