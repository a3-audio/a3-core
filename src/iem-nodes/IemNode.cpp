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
        case IemKind::simpleDecoder:   return {};   // not built yet
        case IemKind::binauralDecoder: return {};   // not built yet
    }
    return {};
}
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

juce::Result IemNode::setParameters (const juce::var& idToValue)
{
    const auto* object = idToValue.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail ("IEM parameters are not a JSON object");
    for (const auto& property : object->getProperties())
    {
        const auto result = setParameter (property.name.toString(), static_cast<float> (property.value));
        if (result.failed())
            return result;
    }
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

    // IEM allocates lazily on the first block after an I/O change; the warm-up
    // takes that allocation here, so the first audio callback does not.
    juce::AudioBuffer<float> silence (ioChannels(), blockSize);
    silence.clear();
    process (silence);
}

int IemNode::preparedBlockSize() const
{
    return blockSize;
}

void IemNode::process (juce::AudioBuffer<float>& io)
{
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
