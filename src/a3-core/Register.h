// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_register (B0 f3345dc7feeb): the catalogue of addresses the system can speak, held
// against the traffic. An address that is in the catalogue and has never arrived is a dead wire. The
// matching is here and not in the page: it is arithmetic, and arithmetic belongs where a test reaches.
#pragma once

#include <functional>
#include <map>
#include <string>

#include <juce_core/juce_core.h>

#include "Traffic.h"

namespace a3core::reg
{
// The messages each collapsed address has carried, numbering added up.
using Counts = std::map<std::string, long long>;

// One address standing for every numbering of itself: a segment that is only digits becomes "0".
// `/MultiEncoder/azimuth0` keeps its 0 -- `azimuth0` is a word.
std::string collapse (const std::string& address);

Counts countsOf (const std::vector<std::pair<std::string, long long>>& addressesAndCounts);

// Whether an address is an instance of the template: `{...}` and `@` stand for one value inside one
// segment, `*` is OSC's wildcard and reaches across `/`, everything else is itself.
std::function<bool (const std::string&)> matcher (const std::string& pattern);

// The register on disk -- {problem, devices, directions, entries} -- or an empty one that says what
// went wrong. Never throws.
juce::var load (const juce::File& file);

// The register with what each address carried: every entry gains `count` and `seen`, the head gains
// `seen` and `unseen`.
juce::var withCounts (const juce::var& registerData, const Counts& counts);

// a3_core_web.register_as_json: the catalogue with the traffic of both tables (understood and unknown).
juce::var registerAsJson (const juce::File& file, Traffic& traffic, double now);
}
