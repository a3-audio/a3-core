// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IemProcessorFactories.h"
#include "PluginProcessor.h"

namespace a3::iem
{
namespace
{
constexpr int busOrder = 3;

// loadConfigFromString is private in IEM and the public loadConfiguration takes
// a file; an explicit instantiation is the one place C++ lets us name it
// without editing the submodule and without touching the disk.
// (The public alternative, setStateInformation with a configString attribute, would also restore every parameter in the state, so it is no fallback for a config alone.)
using LoadConfigFromString = void (SimpleDecoderAudioProcessor::*) (juce::String, bool);
LoadConfigFromString privateLoadConfigFromString();

template <LoadConfigFromString Member>
struct ExposeLoadConfigFromString
{
    friend LoadConfigFromString privateLoadConfigFromString() { return Member; }
};
template struct ExposeLoadConfigFromString<&SimpleDecoderAudioProcessor::loadConfigFromString>;

juce::Result loadConfig (SimpleDecoderAudioProcessor& decoder, const juce::String& json)
{
    if (json.trim().isEmpty())
        return juce::Result::fail ("empty decoder configuration");

    (decoder.*privateLoadConfigFromString()) (json, false);

    const auto message = decoder.getMessageForEditor();
    if (message.isNotEmpty())
        return juce::Result::fail (message);
    if (decoder.getCurrentDecoderConfig() == nullptr)
        return juce::Result::fail ("IEM built no decoder from the configuration");
    return juce::Result::ok();
}
}

IemNode::Parts createSimpleDecoder (int numInputs, int numOutputs)
{
    const auto input = juce::AudioChannelSet::ambisonic (busOrder);
    if (numInputs != input.size() || numOutputs < 1 || numOutputs > SimpleDecoderAudioProcessor::numberOfOutputChannels)
        return {};

    auto processor = std::make_unique<SimpleDecoderAudioProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (input);
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (numOutputs));
    if (! processor->setBusesLayout (layout))
        return {};

    auto* decoder = processor.get();
    return { std::move (processor), &decoder->parameters,
             [decoder] { return decoder->oscParameterInterface.getOSCReceiver().isConnected(); },
             [decoder] (const juce::String& json) { return loadConfig (*decoder, json); } };
}
}
