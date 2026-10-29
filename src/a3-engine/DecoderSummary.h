// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AllRadDesign.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace a3
{
// What a zone plays, for the window: read from its <zone>.decoder.json (an AllRADecoder export) and its
// <zone>.parameters.json (the SimpleDecoder that plays it). Display only; the engine reads the files itself.
struct DecoderSummary
{
    juce::String madeWith;                    // "IEM AllRADecoder v0.9.5"; empty when the file does not say
    juce::String exportedOn;                  // "28 Feb 2026 3:16:23pm"; empty when the file does not say
    int order = 0;                            // from the matrix: 16 columns = 3
    juce::String fileWeights;                 // the decoder file's: none | maxrE | inPhase
    juce::String weights;                     // what SimpleDecoder applies (its "weights"): basic | maxrE | inPhase
    std::vector<Loudspeaker> loudspeakers;    // the file's LoudspeakerLayout; empty when it has none
    int subChannel = 0;                       // swMode 1: swChannel; 0 = no sub
    float lowPassHz = 0.0f, highPassHz = 0.0f;   // the crossover: sub below, tops above
};

// nullopt + error when the decoder file is not JSON or has no Decoder matrix.
std::optional<DecoderSummary> decoderSummary (const juce::String& decoderJson, const juce::var& parameters, juce::String& error);
}
