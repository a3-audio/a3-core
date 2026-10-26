// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <juce_core/juce_core.h>

#include "Net.h"

namespace a3core
{
// A fact the file does not have, or a file that cannot be read (a3_osc.TruthError).
struct TruthError
{
    std::string what;
};

struct Meters
{
    double attackMs, releaseDbPerSecond, peakHoldSeconds;
    bool operator== (const Meters& other) const
    {
        return attackMs == other.attackMs && releaseDbPerSecond == other.releaseDbPerSecond
            && peakHoldSeconds == other.peakHoldSeconds;
    }
};

// The maintainer's network file as a3_osc_join.read_network reads it: the blocks, or absent
// (no file), or why it was refused.
struct NetworkRead
{
    std::optional<juce::var> network;
    std::optional<std::string> problem;
};

NetworkRead readNetwork (const juce::File& file);

// The contract with the network's blocks put in key by key (a3_osc_join.join); neither input changes.
juce::var joinTruth (const juce::var& contract, const std::optional<juce::var>& network);

// a3_osc.Truth, the one reader of a3-osc.json. Methods that look something up throw TruthError
// for a fact the file does not have.
class Truth
{
public:
    explicit Truth (juce::var data);

    // a3_osc.load: the contract joined with `network` (if any). A refused network file leaves the
    // package's blocks (networkProblem says why); a local `meters` block that fails meters() is
    // dropped alone (metersProblem says why).
    static std::variant<Truth, TruthError> load (const juce::File& contract, std::optional<juce::File> network);

    std::string canonical() const;
    std::string fingerprint() const;
    juce::var data() const;      // a deep copy
    juce::var network() const;   // a copy of the network block

    std::string host (const std::string& name) const;
    int port (const std::string& program, const std::string& role) const;
    Endpoint endpoint (const std::string& program, const std::string& role) const;   // "any" is reached as "local"
    juce::var listener (const std::string& program, const std::string& role) const;
    std::vector<juce::var> routes() const;

    std::string pattern (const std::string& key) const;
    // A deep copy of the file's entry for an address key (a3_osc.Truth.addresses()[key]); throws if there is none.
    juce::var addressEntry (const std::string& key) const { return entry (key).clone(); }
    // Range-checked against the file's range for each field given; a placeholder without a field throws.
    std::string address (const std::string& key, const std::map<std::string, int>& fields = {}) const;
    // address() backwards; the first key in file order whose pattern and ranges fit.
    std::optional<std::pair<std::string, std::map<std::string, int>>> match (const std::string& address) const;
    std::pair<int, int> indexRange (const std::string& key, const std::string& field) const;

    std::vector<std::string> vuMeters() const;
    // Missing numbers at their defaults; a wrong type, a value out of range or an unknown key throws.
    Meters meters() const;

    std::optional<std::string> networkProblem, metersProblem;

private:
    struct Part
    {
        bool isField;
        std::string text;   // literal text, or the field's name
    };
    struct Matcher
    {
        std::string key;
        std::vector<Part> parts;
    };

    juce::var entry (const std::string& key) const;

    juce::var truth;
    std::vector<Matcher> matchers;   // compiled once, in file order
};

// a3_osc.truth_path: `given`, else the env value (nullptr or empty = unset), else the installed file.
juce::File truthPath (std::optional<juce::File> given, const char* envTruth);

// a3_osc.network_path. `truthGiven` is true when a path was given or A3_OSC_TRUTH is set: an
// explicit truth never reads the maintainer's real file by accident. `envNetwork` is A3_NETWORK's
// value (nullptr or empty = unset).
std::optional<juce::File> networkPath (bool truthGiven, const juce::File& home, const char* envNetwork);
}
