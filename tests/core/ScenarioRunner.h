// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Replays a scenario of tests/core/scenarios against the C++ Core and compares what it does with the
// transcript tools/parity/record_scenario.py recorded from B1's Python Core. The formats are in
// tests/core/scenarios/README.md.
#pragma once

#include <string>
#include <vector>

#include <juce_core/juce_core.h>

namespace scenarios
{
struct Outcome
{
    juce::Array<juce::var> transcript;      // what the C++ Core did
    std::vector<std::string> differences;   // empty when it did what the Python Core did
    juce::File actualFile;                  // the transcript, written for reading when they differ
};

juce::File directory();
// tests/core/rig: scenarios whose files are the rig's own, given at run time (RigFiles.cpp).
juce::File rigDirectory();

// Runs <name>.json and compares with <name>.expected.json.
Outcome run (const std::string& name);
// Runs a scenario given as data and compares with the given transcript; name only names the files.
Outcome run (const std::string& name, const juce::var& scenario, const juce::var& expected);

// Same order, names, hosts, ports, addresses and tags; floats equal as float32; journal lines and
// their stream equal; files JSON-equal (Python's ==).
std::vector<std::string> compare (const juce::Array<juce::var>& expected, const juce::Array<juce::var>& actual);
}
