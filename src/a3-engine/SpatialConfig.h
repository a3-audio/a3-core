// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Layout.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <utility>
#include <vector>

namespace a3
{
// IEM's value for "3rd order" in the Ambisonic Order choice (0 = auto, 1 = 0th ... 4 = 3rd).
constexpr float iemOrderThree = 4.0f;

struct ZoneConfig
{
    Layout layout;
    juce::var parameters;                                 // SimpleDecoder {id: value}, order set explicitly
    float fixedGainDb = 0.0f;                             // Plan 3 builds it
    float absorbedGainDb = 0.0f;                          // Plan 4 stages; the harness's expected fit
    std::vector<std::pair<float, float>> topDirections;   // az, el per top k (degrees)
};

// Plain JSON files in one directory, so a tool without the engine can read them too:
//   encoder.parameters.json, rec.parameters.json    {id: value}
//   <zone>.decoder.json                             IEM decoder configuration, verbatim
//   <zone>.parameters.json                          SimpleDecoder {id: value}
//   <zone>.zone.json                                {"stages":[{name, db, kind: fixed|plan4}], "topDirections":[[az, el], ...]}
struct SpatialConfig
{
    juce::var encoderParameters, recParameters;
    ZoneConfig main, booth;
    // Task 5: std::array<std::vector<EncoderInput>, 8> sources;
};

// The files keep the template's values; here the Ambisonic order is set to 3
// explicitly (encoder orderSetting, decoder and rec inputOrderSetting), never "auto".
// nullopt + error naming the file and the reason.
std::optional<SpatialConfig> loadSpatialConfig (const juce::File& directory, juce::String& error);
}
