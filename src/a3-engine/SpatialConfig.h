// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Layout.h"
#include <juce_core/juce_core.h>
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace a3
{
// IEM's value for "3rd order" in the Ambisonic Order choice (0 = auto, 1 = 0th ... 4 = 3rd).
constexpr float iemOrderThree = 4.0f;

enum class Tap { moving, steady };

// One strip tap summed into an encoder input: strip 0..3, side 0 = L, 1 = R, gain linear incl. sign.
struct EncoderInput
{
    int strip;
    Tap tap;
    int side;
    float gain;
};

constexpr int numEncoderSources = 8;

struct ZoneConfig
{
    Layout layout;
    juce::var parameters;                                 // SimpleDecoder {id: value}, order set explicitly
    float fixedGainDb = 0.0f;                             // Plan 3 builds it
    float absorbedGainDb = 0.0f;                          // Plan 4 stages; the harness's expected fit
    std::vector<std::pair<float, float>> topDirections;   // az, el per top k (degrees)
};

// Plain JSON files in one directory, so a tool without the engine can read them too:
//   encoder.parameters.json, rec.parameters.json,
//   fx.parameters.json, phones.parameters.json      {id: value}
//   <zone>.decoder.json                             IEM decoder configuration, verbatim
//   <zone>.parameters.json                          SimpleDecoder {id: value}
//   encoder.inputs.json                             {"sources":[[{strip 1..4, tap moving|steady, side L|R, gain}, ...] x 8]}
//   <zone>.zone.json                                {"stages":[{name, db, kind: fixed|plan4}], "topDirections":[[az, el], ...]}
struct SpatialConfig
{
    juce::var encoderParameters, recParameters;
    juce::var fxParameters;       // DualDelay {id: value}, order set explicitly
    juce::var phonesParameters;   // the phones BinauralDecoder {id: value}, order set explicitly
    ZoneConfig main, booth;
    std::array<std::vector<EncoderInput>, numEncoderSources> sources;   // encoder input i = sum of its entries
};

// The files keep the template's values; here the Ambisonic order is set to 3
// explicitly (encoder orderSetting, decoder and rec inputOrderSetting), never "auto".
// nullopt + error naming the file and the reason.
std::optional<SpatialConfig> loadSpatialConfig (const juce::File& directory, juce::String& error);
}
