// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace a3::links
{
struct LinkMap
{
    std::map<std::string, std::vector<std::string>> jack;                  // own port -> peer ports
    std::vector<std::pair<std::string, std::string>> directLinks;          // foreign -> foreign, made and owned by us
};

juce::var toJson (const LinkMap&);
// nullopt + error: names the entry that is wrong. Every own-side name must be one of the two lists.
std::optional<LinkMap> parseLinkMap (const juce::var& section, const std::vector<std::string>& ownInputs,
                                     const std::vector<std::string>& ownOutputs, juce::String& error);
}
