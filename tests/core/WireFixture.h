// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// tests/core/data/parity/wire.json (tools/parity/record_wire.py): python-osc 1.10.2 and Python's
// numbers, and the Python values it records rebuilt as OSC arguments.
#pragma once

#include <charconv>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#include "Json.h"
#include "OscWire.h"

namespace wirefixture
{
inline const juce::var& wire()
{
    static const juce::var loaded = [] {
        juce::String error;
        auto value = a3core::parseJson (juce::File (A3_CORE_TEST_DATA "/parity/wire.json").loadFileAsString(), error);
        EXPECT_EQ (error, juce::String());
        return value;
    }();
    return loaded;
}

inline std::vector<uint8_t> base64 (const juce::var& encoded)
{
    juce::MemoryOutputStream decoded;
    EXPECT_TRUE (juce::Base64::convertFromBase64 (decoded, encoded.toString()));
    auto* bytes = static_cast<const uint8_t*> (decoded.getData());
    return { bytes, bytes + decoded.getDataSize() };
}

inline std::vector<uint8_t> hex (const juce::var& text)
{
    const auto s = text.toString().toStdString();
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        out.push_back ((uint8_t) std::stoi (s.substr (i, 2), nullptr, 16));
    return out;
}

inline std::string py (const juce::var& value) { return value["py"].toString().toStdString(); }

inline double floatOf (const juce::var& value) { return std::strtod (value["hex"].toString().toRawUTF8(), nullptr); }

inline std::string strOf (const juce::var& value)
{
    const auto bytes = base64 (value["utf8_b64"]);
    return { bytes.begin(), bytes.end() };
}

// A recorded Python int, if it fits a long long.
inline std::optional<long long> intOf (const juce::var& value)
{
    const auto text = value["value"].toString().toStdString();
    long long out = 0;
    const auto [end, error] = std::from_chars (text.data(), text.data() + text.size(), out);
    if (error != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return out;
}

// The argument a handler receives for this Python value; a float as 'd' (Python's float is a double).
inline a3core::OscArg received (const juce::var& value)
{
    const auto type = py (value);
    if (type == "None") return { 'N', {} };
    if (type == "bool") return a3core::pyBool ((bool) value["value"]);
    if (type == "int") return a3core::pyInt (*intOf (value));
    if (type == "float") return { 'd', floatOf (value) };
    if (type == "str") return a3core::pyStr (strOf (value));
    if (type == "bytes") return { 'b', base64 (value["b64"]) };
    ADD_FAILURE() << "no OSC argument for " << type;
    return { 'N', {} };
}
}
