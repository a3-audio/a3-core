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

IemNode::Parts createDualDelay (int numInputs, int numOutputs)
{
    const auto bus = juce::AudioChannelSet::ambisonic (busOrder);
    if (numInputs != bus.size() || numOutputs != bus.size())
        return {};

    auto processor = std::make_unique<DualDelayAudioProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (bus);
    layout.outputBuses.add (bus);
    if (! processor->setBusesLayout (layout))
        return {};

    auto* delay = processor.get();
    return { std::move (processor), &delay->parameters,
             [delay] { return delay->oscParameterInterface.getOSCReceiver().isConnected(); } };
}
}
