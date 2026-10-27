// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the tests of the traffic, the seen list and the register: what tools/parity/record_traffic.py
// recorded from B0 (a3-core f3345dc7feeb), a clock the test turns, and the helpers that feed a Traffic.
#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#include "Json.h"
#include "Net.h"
#include "OscWire.h"
#include "Traffic.h"

namespace recordedtraffic
{
inline juce::var load (const char* name)
{
    juce::String error;
    auto value = a3core::parseJson (juce::File (A3_CORE_TEST_DATA "/parity/traffic").getChildFile (name).loadFileAsString(), error);
    EXPECT_EQ (error, juce::String()) << name;
    return value;
}

inline const juce::var& fixture (const char* name)
{
    static std::map<std::string, juce::var> cache;
    auto found = cache.find (name);
    if (found == cache.end())
        found = cache.emplace (name, load (name)).first;
    return found->second;
}

inline std::string dump (const juce::var& value) { return a3core::dumpIndented (value, 1); }

inline void expectJsonEqual (const juce::var& expected, const juce::var& actual, const std::string& what)
{
    EXPECT_TRUE (a3core::pythonEquals (expected, actual)) << what << "\nexpected " << dump (expected) << "\nactual   " << dump (actual);
}

inline std::vector<uint8_t> bytesOfHex (const juce::String& hex)
{
    std::vector<uint8_t> bytes;
    for (int i = 0; i + 1 < hex.length(); i += 2)
        bytes.push_back (static_cast<uint8_t> (hex.substring (i, i + 2).getHexValue32()));
    return bytes;
}

// A recorded Python value ["float", 0.5] as the OscArg python-osc would have delivered it; none for None.
struct Value
{
    std::optional<a3core::OscArg> arg;
    const a3core::OscArg* get() const { return arg ? &*arg : nullptr; }
};

inline Value valueOf (const juce::var& spec)
{
    const auto kind = spec[0].toString();
    const auto& raw = spec[1];
    if (kind == "float")
    {
        if (raw.isString())
        {
            const auto word = raw.toString();
            return { a3core::pyFloat (word == "nan"    ? std::numeric_limits<double>::quiet_NaN()
                                      : word == "inf" ? std::numeric_limits<double>::infinity()
                                                       : -std::numeric_limits<double>::infinity()) };
        }
        return { a3core::pyFloat (static_cast<double> (raw)) };
    }
    if (kind == "int")
        return { a3core::pyInt (static_cast<juce::int64> (raw)) };
    if (kind == "str")
        return { a3core::pyStr (raw.toString().toStdString()) };
    if (kind == "bool")
        return { a3core::pyBool (static_cast<bool> (raw)) };
    if (kind == "bytes")
        return { a3core::OscArg { 'b', bytesOfHex (raw.toString()) } };
    if (kind == "tuple")
    {
        std::vector<uint8_t> four;
        for (const auto& item : *raw.getArray())
            four.push_back (static_cast<uint8_t> (static_cast<int> (item)));
        return { a3core::OscArg { 'm', four } };
    }
    return {};   // NoneType
}

inline a3core::Direction directionOf (const juce::String& name) { return name == "in" ? a3core::Direction::in : a3core::Direction::out; }

inline void play (a3core::Traffic& traffic, const juce::var& ops)
{
    for (const auto& op : *ops.getArray())
    {
        const auto value = valueOf (op["value"]);
        const auto at = static_cast<double> (op["t"]);
        if (op["op"].toString() == "seen")
            traffic.seen (directionOf (op["dir"].toString()), op["address"].toString().toStdString(), value.get(),
                          op["peer"].toString().toStdString(), at);
        else
            traffic.unknown (op["address"].toString().toStdString(), value.get(), op["peer"].toString().toStdString(), at);
    }
}

// The clock the SeenFile tests turn.
struct ScriptedClock : a3core::Clock
{
    double mono = 0, wallTime = 0;
    double monotonic() override { return mono; }
    double wall() override { return wallTime; }
};

// Strictly rising times for tests that do not care which: one tick per call, from any thread.
inline double tick()
{
    static std::atomic<long long> counter { 0 };
    return 1.0 + static_cast<double> (counter.fetch_add (1)) * 0.001;
}

inline double steadySeconds()
{
    return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline void seenFloat (a3core::Traffic& traffic, a3core::Direction direction, const std::string& address, double value,
                       const std::string& peer)
{
    const auto arg = a3core::pyFloat (value);
    traffic.seen (direction, address, &arg, peer, tick());
}

inline void seenInt (a3core::Traffic& traffic, a3core::Direction direction, const std::string& address, long long value,
                     const std::string& peer)
{
    const auto arg = a3core::pyInt (value);
    traffic.seen (direction, address, &arg, peer, tick());
}

inline void unknownOf (a3core::Traffic& traffic, const std::string& address, const a3core::OscArg& arg, const std::string& peer = "reaper")
{
    traffic.unknown (address, &arg, peer, tick());
}

// A row of a snapshot by direction and address.
inline const a3core::TrafficRow* rowOf (const a3core::TrafficSnapshot& snapshot, a3core::Direction direction, const std::string& address)
{
    for (const auto& row : snapshot.rows)
        if (row.direction == direction && row.address == address)
            return &row;
    return nullptr;
}
}
