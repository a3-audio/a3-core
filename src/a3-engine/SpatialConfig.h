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

// The two loudspeaker zones, each a <zone>.decoder.json + <zone>.parameters.json pair.
enum class SpeakerZone { main, booth };
inline const char* zoneName (SpeakerZone zone) { return zone == SpeakerZone::main ? "main" : "booth"; }

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

// The four fixed bed sources (the template's encoder inputs 5..8): their place and
// gain from encoder.parameters, their side from encoder.inputs.
constexpr int numBedSources = 4;
constexpr int firstBedSourceInFile = 4;
struct BedSource { float azimuth, elevation, gainDb; int side; };   // side 0 = L, 1 = R
std::array<BedSource, numBedSources> bedFrom (const SpatialConfig&);

// The files keep the template's values; here the Ambisonic order is set to 3
// explicitly (encoder orderSetting, decoder and rec inputOrderSetting), never "auto".
// nullopt + error naming the file and the reason.
std::optional<SpatialConfig> loadSpatialConfig (const juce::File& directory, juce::String& error);
// `mainDirectory`'s config with its booth zone taken from `boothDirectory`'s (the same directory: one load).
// Both must load whole; a refusal names the directory it came from.
std::optional<SpatialConfig> loadSpatialConfig (const juce::File& mainDirectory, const juce::File& boothDirectory, juce::String& error);
}
