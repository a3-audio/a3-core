// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// B0's shipped truth (a3-core f3345dc7feeb), from the canonical text Task 3 recorded: the truth
// the Python tests of the ported modules load with a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json").
#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "Json.h"
#include "Truth.h"

namespace b0
{
inline const a3core::Truth& truth()
{
    static const a3core::Truth loaded = [] {
        juce::String error;
        const auto fixture = a3core::parseJson (juce::File (A3_CORE_TEST_DATA "/parity/json.json").loadFileAsString(), error);
        juce::MemoryOutputStream canonical;
        juce::Base64::convertFromBase64 (canonical, fixture["truth"]["canonical_b64"].toString());
        return a3core::Truth (a3core::parseJson (canonical.toString(), error));
    }();
    return loaded;
}

inline std::vector<std::string> strings (const juce::var& list)
{
    std::vector<std::string> out;
    if (const auto* items = list.getArray())
        for (const auto& item : *items)
            out.push_back (item.toString().toStdString());
    return out;
}
}
