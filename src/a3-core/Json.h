// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

#include <juce_core/juce_core.h>

namespace a3core
{
// Keeps the key order of the text (juce::DynamicObject is ordered). `error` is empty on success.
juce::var parseJson (const juce::String& text, juce::String& error);

// json.dumps(sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode(): the bytes every
// device hashes alike. Keys sort by code point, which is the byte order of their UTF-8.
std::string canonicalBytes (const juce::var& value);

std::string sha256Hex (const std::string& bytes);

// float.__repr__: shortest digits that round-trip, fixed for decimal exponent -4 <= e < 16.
std::string pythonRepr (double value);

// json.dumps(indent=n): insertion order, ensure_ascii=True (state files use 2, network.json 1).
std::string dumpIndented (const juce::var& value, int indent);

// json.dumps(value): one line, separators ", " and ": ", insertion order, ensure_ascii=True.
std::string dumpPython (const juce::var& value);

// bool(x) of a value read from JSON.
bool pythonTruthy (const juce::var& value);

// x == y as Python compares what json.loads made: numbers by value (1 == 1.0 == True), objects
// regardless of key order, arrays element by element. `sameNan`: a NaN equals a NaN, as it does inside
// a Python container when both are the same float object (the comparison tries identity first).
bool pythonEquals (const juce::var& a, const juce::var& b, bool sameNan = false);

// float(x) for a JSON number or boolean; nullopt for anything else (a text is not read as a number).
std::optional<double> numberOf (const juce::var& value);
}
