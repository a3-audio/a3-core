// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IemProcessorFactories.h"
#include "PluginProcessor.h"

namespace a3::iem
{
namespace
{
constexpr int busOrder = 3;
}

IemNode::Parts createMultiEncoder (int numInputs, int numOutputs)
{
    const auto output = juce::AudioChannelSet::ambisonic (busOrder);
    if (numInputs < 1 || numInputs > maxNumberOfInputs || numOutputs != output.size())
        return {};

    auto processor = std::make_unique<MultiEncoderAudioProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::discreteChannels (numInputs));
    layout.outputBuses.add (output);
    if (! processor->setBusesLayout (layout))
        return {};

    auto* encoder = processor.get();
    return { std::move (processor), &encoder->parameters,
             [encoder] { return encoder->oscParameterInterface.getOSCReceiver().isConnected(); } };
}
}
