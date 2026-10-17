// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace a3
{
// A loudspeaker zone as the engine wires it: N tops (N >= 1) and an optional sub.
// The counts are data, never code: whatever the decoder file says.
struct Layout
{
    juce::String decoderJson;            // verbatim IEM ConfigurationHelper text
    int subChannel = 0;                  // 1-based decoder output channel; 0 = no sub
    std::vector<int> topChannels;        // 1-based, in Routing order: top_k = topChannels[k-1]

    bool hasSub() const { return subChannel > 0; }
    int numOutputs() const;              // max(subChannel, max(topChannels)): the decoder's output channel count
};

// Reads the routing through IEM's own decoder parser. The sub comes from the
// zone's SimpleDecoder parameters (swMode 1 = discrete sub on swChannel,
// swMode 0 = none), because the rig's decoder files carry no SubwooferChannel.
// Refuses, with the rule in the error: bad JSON or an IEM parse failure, not
// order 3 (16 matrix columns), no tops, a duplicate Routing channel, a sub on a
// top's channel, a channel outside 1..64, a swMode other than 0 or 1.
// Gaps in the routing are allowed.
std::optional<Layout> parseLayout (const juce::String& decoderJson, const juce::var& parameters, juce::String& error);
}
