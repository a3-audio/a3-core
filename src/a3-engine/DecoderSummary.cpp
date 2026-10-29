// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DecoderSummary.h"
#include <cmath>

namespace a3
{
namespace
{
// AllRADecoder writes "This configuration file was created with the IEM AllRADecoder v0.9.5 plug-in. <date>".
const juce::String createdWith = "created with the ";
const juce::String pluginEnd = " plug-in. ";

double numberOr (const juce::var& object, const char* id, double fallback)
{
    const auto value = object.getProperty (id, juce::var());
    return (value.isInt() || value.isInt64() || value.isDouble()) ? (double) value : fallback;
}

// SimpleDecoder's "weights" choice, in its order.
juce::String simpleDecoderWeights (int index)
{
    static const juce::StringArray names { "basic", "maxrE", "inPhase" };
    return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
}

std::vector<Loudspeaker> loudspeakersOf (const juce::var& file)
{
    std::vector<Loudspeaker> speakers;
    const auto rows = file["LoudspeakerLayout"]["Loudspeakers"];
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        speakers.push_back ({ (float) numberOr (row, "Azimuth", 0), (float) numberOr (row, "Elevation", 0),
                              (float) numberOr (row, "Radius", 1), (bool) row.getProperty ("IsImaginary", false),
                              (int) numberOr (row, "Channel", 0), (float) numberOr (row, "Gain", 1) });
    }
    return speakers;
}

void readDescription (const juce::String& description, DecoderSummary& summary)
{
    if (! description.contains (createdWith) || ! description.contains (pluginEnd))
        return;
    summary.madeWith = description.fromFirstOccurrenceOf (createdWith, false, false).upToFirstOccurrenceOf (pluginEnd, false, false);
    summary.exportedOn = description.fromFirstOccurrenceOf (pluginEnd, false, false).trim();
}
}

std::optional<DecoderSummary> decoderSummary (const juce::String& decoderJson, const juce::var& parameters, juce::String& error)
{
    juce::var file;
    if (const auto parse = juce::JSON::parse (decoderJson, file); parse.failed())
    {
        error = "the decoder file is not JSON: " + parse.getErrorMessage();
        return std::nullopt;
    }
    const auto matrix = file["Decoder"]["Matrix"];
    if (! matrix.isArray() || matrix.size() == 0 || ! matrix[0].isArray())
    {
        error = "the decoder file has no Decoder Matrix";
        return std::nullopt;
    }

    DecoderSummary summary;
    readDescription (file["Description"].toString(), summary);
    summary.order = juce::roundToInt (std::sqrt ((double) matrix[0].size())) - 1;
    summary.fileWeights = file["Decoder"]["Weights"].toString();
    const int applied = juce::roundToInt (numberOr (parameters, "weights", -1));
    summary.weights = applied >= 0 ? simpleDecoderWeights (applied) : summary.fileWeights;
    summary.loudspeakers = loudspeakersOf (file);
    if (juce::roundToInt (numberOr (parameters, "swMode", 0)) == 1)
        summary.subChannel = juce::roundToInt (numberOr (parameters, "swChannel", 1));
    summary.lowPassHz = (float) numberOr (parameters, "lowPassFrequency", 0);
    summary.highPassHz = (float) numberOr (parameters, "highPassFrequency", 0);
    return summary;
}
}
