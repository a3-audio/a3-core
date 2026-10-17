// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Layout.h"
#include <JuceHeader.h>
#define CONFIGURATIONHELPER_ENABLE_DECODER_METHODS 1
#include "ConfigurationHelper.h"
#include "ReferenceCountedDecoder.h"
#include <algorithm>
#include <set>

namespace a3
{
namespace
{
constexpr size_t order3Columns = 16;
constexpr int highestOutputChannel = 64;   // swChannel's range in IEM's SimpleDecoder
constexpr int swModeOff = 0;
constexpr int swModeDiscrete = 1;

struct SubSetting { int mode = swModeOff; int channel = 1; };

double numberOrDefault (const juce::var& parameters, const char* id, double fallback)
{
    const auto value = parameters.getProperty (id, juce::var());
    return (value.isInt() || value.isInt64() || value.isDouble()) ? (double) value : fallback;
}

SubSetting subSettingFrom (const juce::var& parameters)
{
    return { juce::roundToInt (numberOrDefault (parameters, "swMode", swModeOff)),
             juce::roundToInt (numberOrDefault (parameters, "swChannel", 1)) };
}

bool parseDecoder (const juce::String& json, ReferenceCountedDecoder::Ptr& decoder, juce::String& error)
{
    juce::var parsed;
    if (const auto parse = juce::JSON::parse (json, parsed); parse.failed())
    {
        error = "decoder configuration is not valid JSON: " + parse.getErrorMessage();
        return false;
    }
    if (const auto result = ConfigurationHelper::parseVarForDecoder (parsed, &decoder); result.failed())
    {
        error = "IEM refuses the decoder configuration: " + result.getErrorMessage();
        return false;
    }
    return true;
}

bool subSettingIsLegal (const SubSetting& sub, juce::String& error)
{
    if (sub.mode != swModeOff && sub.mode != swModeDiscrete)
    {
        error = "swMode " + juce::String (sub.mode) + " is not supported: 0 (no sub) or 1 (discrete sub) only";
        return false;
    }
    if (sub.mode == swModeDiscrete && (sub.channel < 1 || sub.channel > highestOutputChannel))
    {
        error = "swChannel " + juce::String (sub.channel) + " is outside 1.." + juce::String (highestOutputChannel);
        return false;
    }
    return true;
}

bool channelsAreLegal (const std::vector<int>& tops, int sub, juce::String& error)
{
    std::set<int> seen;
    for (const int channel : tops)
    {
        if (channel < 1 || channel > highestOutputChannel)
        {
            error = "Routing channel " + juce::String (channel) + " is outside 1.." + juce::String (highestOutputChannel);
            return false;
        }
        if (! seen.insert (channel).second)
        {
            error = "Routing lists output channel " + juce::String (channel) + " twice";
            return false;
        }
    }
    if (sub > 0 && seen.count (sub) > 0)
    {
        error = "the sub (swChannel " + juce::String (sub) + ") sits on the output channel of a top";
        return false;
    }
    return true;
}

std::vector<int> routingOf (ReferenceCountedDecoder& decoder)
{
    std::vector<int> channels;
    for (const int zeroBased : decoder.getRoutingArrayReference())
        channels.push_back (zeroBased + 1);
    return channels;
}
}

int Layout::numOutputs() const
{
    int highest = subChannel;
    for (const int channel : topChannels)
        highest = std::max (highest, channel);
    return highest;
}

std::optional<Layout> parseLayout (const juce::String& decoderJson, const juce::var& parameters, juce::String& error)
{
    ReferenceCountedDecoder::Ptr decoder;
    if (! parseDecoder (decoderJson, decoder, error))
        return std::nullopt;

    if (decoder->getMatrix().getNumColumns() != order3Columns)
    {
        error = "the decoder is not order 3: " + juce::String ((int) decoder->getMatrix().getNumColumns())
                + " matrix columns, 16 expected";
        return std::nullopt;
    }

    const auto sub = subSettingFrom (parameters);
    if (! subSettingIsLegal (sub, error))
        return std::nullopt;

    Layout layout;
    layout.decoderJson = decoderJson;
    layout.topChannels = routingOf (*decoder);
    layout.subChannel = sub.mode == swModeDiscrete ? sub.channel : 0;

    if (layout.topChannels.empty())
    {
        error = "the decoder has no loudspeakers (N < 1)";
        return std::nullopt;
    }
    if (! channelsAreLegal (layout.topChannels, layout.subChannel, error))
        return std::nullopt;
    return layout;
}
}
