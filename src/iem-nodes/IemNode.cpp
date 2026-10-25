// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IemNode.h"
#include "IemProcessorFactories.h"

namespace a3
{
namespace
{
IemNode::Parts createParts (IemKind kind, int numInputs, int numOutputs)
{
    switch (kind)
    {
        case IemKind::multiEncoder:    return iem::createMultiEncoder (numInputs, numOutputs);
        case IemKind::simpleDecoder:   return iem::createSimpleDecoder (numInputs, numOutputs);
        case IemKind::binauralDecoder: return iem::createBinauralDecoder (numInputs, numOutputs);
        case IemKind::stereoEncoder:   return iem::createStereoEncoder (numInputs, numOutputs);
    }
    return {};
}

// SimpleDecoder resets swMode to 0 when it picks up a config without a
// SubwooferChannel, and the rig's configs have none: the sub comes from the
// parameters. Whatever was set before a load or a warm-up is what stays.
class SubwooferSettingsKept
{
public:
    explicit SubwooferSettingsKept (juce::AudioProcessorValueTreeState& parameters)
    {
        for (const char* id : { "swMode", "swChannel" })
            if (auto* parameter = parameters.getParameter (id))
                kept.push_back ({ parameter, parameter->getValue() });
    }

    ~SubwooferSettingsKept()
    {
        for (const auto& [parameter, value] : kept)
            if (parameter->getValue() != value)
                parameter->setValueNotifyingHost (value);
    }

private:
    std::vector<std::pair<juce::RangedAudioParameter*, float>> kept;
};
}

std::unique_ptr<IemNode> IemNode::create (IemKind kind, int numInputs, int numOutputs)
{
    auto parts = createParts (kind, numInputs, numOutputs);
    if (parts.processor == nullptr)
        return nullptr;
    return std::unique_ptr<IemNode> (new IemNode (std::move (parts)));
}

IemNode::IemNode (Parts p) : parts (std::move (p)) {}

IemNode::~IemNode() = default;

juce::RangedAudioParameter* IemNode::findParameter (const juce::String& id) const
{
    return parts.parameters->getParameter (id);
}

juce::Result IemNode::setParameter (const juce::String& id, float value)
{
    auto* parameter = findParameter (id);
    if (parameter == nullptr)
        return juce::Result::fail ("unknown IEM parameter: " + id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    return juce::Result::ok();
}

juce::Result IemNode::validateParameters (const juce::DynamicObject& idToValue) const
{
    for (const auto& property : idToValue.getProperties())
    {
        const auto& value = property.value;
        if (! (value.isInt() || value.isInt64() || value.isDouble() || value.isBool()))
            return juce::Result::fail ("IEM parameter " + property.name.toString() + " is not a number");
        if (findParameter (property.name.toString()) == nullptr)
            return juce::Result::fail ("unknown IEM parameter: " + property.name.toString());
    }
    return juce::Result::ok();
}

juce::Result IemNode::setParameters (const juce::var& idToValue)
{
    const auto* object = idToValue.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail ("IEM parameters are not a JSON object");
    const auto valid = validateParameters (*object);
    if (valid.failed())
        return valid;
    for (const auto& property : object->getProperties())
        setParameter (property.name.toString(), static_cast<float> (property.value));
    return juce::Result::ok();
}

int IemNode::ioChannels() const
{
    return juce::jmax (parts.processor->getTotalNumInputChannels(), parts.processor->getTotalNumOutputChannels());
}

void IemNode::prepare (double sampleRate, int newBlockSize)
{
    blockSize = newBlockSize;
    parts.processor->setRateAndBufferSizeDetails (sampleRate, blockSize);
    parts.processor->prepareToPlay (sampleRate, blockSize);
    warmUp();
}

void IemNode::warmUp()
{
    const SubwooferSettingsKept subwooferSettings (*parts.parameters);
    // IEM allocates lazily on the first block after an I/O change; the warm-up
    // takes that allocation here, so the first audio callback does not. Off the
    // audio thread the lock may block: the warm-up must never be skipped.
    juce::AudioBuffer<float> silence (ioChannels(), blockSize);
    silence.clear();
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedLock lock (parts.processor->getCallbackLock());
    parts.processor->processBlock (silence, noMidi);
}

juce::Result IemNode::loadDecoderConfig (const juce::String& json)
{
    if (! parts.loadDecoderConfig)
        return juce::Result::fail ("this IEM node has no decoder configuration");
    const SubwooferSettingsKept subwooferSettings (*parts.parameters);
    const auto result = parts.loadDecoderConfig (json);
    if (result.failed())
        return result;
    if (blockSize > 0)
        warmUp();   // the new decoder is picked up in processBlock, not here
    return juce::Result::ok();
}

int IemNode::preparedBlockSize() const
{
    return blockSize;
}

void IemNode::process (juce::AudioBuffer<float>& io)
{
    jassert (io.getNumChannels() >= ioChannels());
    jassert (io.getNumSamples() <= blockSize);
    juce::ScopedNoDenormals noDenormals;
    // IEM swaps its I/O buffers under the callback lock when a setting changes
    // on the message thread; never wait for it on the audio thread.
    const juce::ScopedTryLock lock (parts.processor->getCallbackLock());
    if (! lock.isLocked())
    {
        io.clear();
        return;
    }
    parts.processor->processBlock (io, noMidi);
}

void IemNode::setStereoPose (const StereoPose& pose)
{
    if (parts.setStereoPose)
        parts.setStereoPose (pose);
}

std::atomic<float>* IemNode::rawParameter (const juce::String& id)
{
    return parts.parameters->getRawParameterValue (id);
}

float IemNode::legalValue (const juce::String& id, float value) const
{
    const auto* parameter = findParameter (id);
    if (parameter == nullptr)
        return value;
    return parameter->convertFrom0to1 (parameter->convertTo0to1 (value));
}

bool IemNode::listensForOsc() const
{
    return parts.listensForOsc();
}

juce::MemoryBlock IemNode::state()
{
    juce::MemoryBlock block;
    parts.processor->getStateInformation (block);
    return block;
}

juce::AudioProcessor& IemNode::processor()
{
    return *parts.processor;
}
}
