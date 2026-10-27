// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IemProcessorFactories.h"
#include "PluginProcessor.h"

namespace a3::iem
{
IemNode::Parts createBinauralDecoder (int numInputs, int numOutputs)
{
    constexpr int busOrder = 3;
    const auto input = juce::AudioChannelSet::ambisonic (busOrder);
    const auto output = juce::AudioChannelSet::stereo();
    if (numInputs != input.size() || numOutputs != output.size())
        return {};

    auto processor = std::make_unique<BinauralDecoderAudioProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (input);
    layout.outputBuses.add (output);
    if (! processor->setBusesLayout (layout))
        return {};

    auto* decoder = processor.get();
    return { std::move (processor), &decoder->parameters,
             [decoder] { return decoder->oscParameterInterface.getOSCReceiver().isConnected(); } };
}
}
