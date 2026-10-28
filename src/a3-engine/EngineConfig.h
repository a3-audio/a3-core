// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DjFilter.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <string>
#include <vector>

namespace a3
{
// Trims in dB. A zone without a trim has no default: it stays closed, never 0 dB.
struct Trims
{
    std::optional<float> main, booth, phones;
    float rec = 0.0f;
};

struct ModulatedParameter
{
    juce::String parameter;
    float base = 0.0f, depth = 0.0f, rateHz = 0.0f;
    juce::String shape;
};

struct EngineConfig
{
    int channels = 4;                 // N >= 1, no upper cap
    int decks = 2;                    // deck slots A, B, ...; each 16 + aux 16 + cue 9 inputs
    int analogInputs = 12;
    // Per channel (size == channels), 1-based pair (2p-1, 2p); 0 = deck-only, no encoder source.
    std::vector<int> analogPairs { 1, 2, 3, 4 };
    Trims trims;
    juce::String phonesHeadphoneEq = "off";   // "off" or an IEM BinauralDecoder headphone model; never the rec decoder's
    FilterKind filterKind = FilterKind::svf;
    // The values are copied from Step 0's measurement of the REAPER template ("best reading").
    std::vector<ModulatedParameter> delayModulation {
        { "pitchL", 0.0f, 18.0f, 0.124573f, "random" },
        { "pitchR", 0.0f, 18.0f, 0.018817f, "random" } };
};

// IEM's headphone-EQ models in its own order (BinauralDecoder v1.16.0): choice 0 = off, 1... = these.
const std::vector<juce::String>& headphoneEqModels();
// "off" -> 0, a model -> its 1-based place, anything else nullopt.
std::optional<int> headphoneEqChoice (const juce::String& name);

bool operator== (const Trims&, const Trims&);
bool operator== (const ModulatedParameter&, const ModulatedParameter&);
bool operator== (const EngineConfig&, const EngineConfig&);

// Keys an older engine section may still name: ignored on load, whatever their value, never written.
const std::vector<std::string>& retiredEngineKeys();

// Refuses with a reason in `error`; nothing is filled in for a broken or partial section.
std::optional<EngineConfig> parseEngineConfig (const juce::var& engineSection, juce::String& error);
juce::var toJson (const EngineConfig&);   // round-trips through parseEngineConfig
}
