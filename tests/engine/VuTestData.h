// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
#include <juce_core/juce_core.h>

// The meters' test data: the truth copy's vu_meters and vu pattern, vu-points.json, vu-bundles.json.
namespace a3test
{
inline juce::var jsonFile (const char* path) { return juce::JSON::parse (juce::File (path)); }

inline std::vector<std::string> truthVuMeters()
{
    std::vector<std::string> names;
    const auto list = jsonFile (A3_TRUTH_COPY)["vu_meters"];
    for (int i = 0; i < list.size(); ++i)
        names.push_back (list[i].toString().toStdString());
    return names;
}

inline std::string truthVuPattern()
{
    return jsonFile (A3_TRUTH_COPY)["addresses"]["vu"]["pattern"].toString().toStdString();
}

inline juce::var vuPoints() { return jsonFile (A3_VU_POINTS)["meters"]; }
}
