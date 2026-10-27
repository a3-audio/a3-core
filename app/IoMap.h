// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EnginePorts.h"
#include <juce_core/juce_core.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace a3app
{
struct IoMap
{
    std::map<std::string, std::vector<std::string>> jack;                  // engine port -> peer ports
    std::vector<std::pair<std::string, std::string>> directLinks;          // foreign -> foreign, made and owned by the engine (R4)
    std::map<std::string, int> deviceChannels;                             // engine port -> device channel (0-based)
};

juce::var toJson (const IoMap&);
// nullopt + error: names the entry that is wrong. Every engine-side name must be one of `ports`.
std::optional<IoMap> parseIoMap (const juce::var& section, const PortNames& ports, juce::String& error);
}
