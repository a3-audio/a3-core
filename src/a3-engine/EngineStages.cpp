// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineStages.h"
#include <algorithm>
#include <stdexcept>
#include "Laws.h"

namespace a3
{
namespace engine
{
namespace
{
// A buffer view with more channels than JUCE's preallocated table would allocate.
constexpr int maxViewChannels = 31;
}

std::unique_ptr<IemNode> createNode (IemKind kind, int numInputs, int numOutputs, const juce::String& what)
{
    auto node = IemNode::create (kind, numInputs, numOutputs);
    if (node == nullptr)
        throw std::runtime_error ((what + ": IEM refused " + juce::String (numInputs) + " in / "
                                   + juce::String (numOutputs) + " out").toStdString());
    return node;
}

void require (const juce::Result& result, const juce::String& what)
{
    if (result.failed())
        throw std::runtime_error ((what + ": " + result.getErrorMessage()).toStdString());
}

void runNode (IemNode& node, juce::AudioBuffer<float>& buffer, int count)
{
    jassert (buffer.getNumChannels() <= maxViewChannels);
    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
    node.process (view);
}
}

void GainStage::prepare (double sampleRate)
{
    gain.prepare (sampleRate);
    silence.prepare (sampleRate);
}

void GainStage::set (float control)
{
    gain.setParameters ((float) laws::slopeVolume (control), 1.0f);
    silence.setOpen (! laws::isSilent (control));
}

void GainStage::process (float* const* io, int numSamples, int numChannels)
{
    gain.process (io, numSamples, numChannels);
    silence.process (io, numSamples, numChannels);
}

void WeightRamp::prepare (double sampleRate)
{
    step = (float) (1.0 / std::max (1.0, 0.010 * sampleRate));
    current = target = 0.0f;
    started = false;
}

void WeightRamp::setTarget (float weight)
{
    target = weight;
    if (! started)
    {
        current = weight;
        started = true;
    }
}

void WeightRamp::render (float* curve, int numSamples)
{
    int i = 0;
    for (; i < numSamples && current != target; ++i)
    {
        current = current < target ? std::min (target, current + step) : std::max (target, current - step);
        curve[i] = current;
    }
    std::fill (curve + i, curve + numSamples, current);
    silence = current == 0.0f && (numSamples == 0 || curve[0] == 0.0f);   // monotonic: first and last 0 = all 0
}

ZoneDecoder::ZoneDecoder (const ZoneConfig& config, const juce::String& name)
    : outputs (config.layout.numOutputs()), subChannel (config.layout.subChannel), topChannels (config.layout.topChannels)
{
    decoder = engine::createNode (IemKind::simpleDecoder, engine::busChannels, outputs, name);
    engine::require (decoder->setParameters (config.parameters), name + ".parameters.json");
    engine::require (decoder->loadDecoderConfig (config.layout.decoderJson), name + ".decoder.json");
}

void ZoneDecoder::prepare (double sampleRate, int blockSize)
{
    buffer.setSize (std::max (engine::busChannels, decoder->processor().getTotalNumOutputChannels()), blockSize);
    stage.prepare (sampleRate);
    decoder->prepare (sampleRate, blockSize);
}

void ZoneDecoder::process (const juce::AudioBuffer<float>& bus16, const ZoneOutputs& out, int offset, int count,
                           OutputGate& gate, bool open)
{
    render (bus16, count);
    emit (out, offset, count, gate, open);
}

void ZoneDecoder::render (const juce::AudioBuffer<float>& bus16, int count)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        if (ch < engine::busChannels)
            juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), bus16.getReadPointer (ch), count);
        else
            juce::FloatVectorOperations::clear (buffer.getWritePointer (ch), count);
    }
    engine::runNode (*decoder, buffer, count);
    stage.process (buffer.getArrayOfWritePointers(), count, outputs);
}

void ZoneDecoder::emit (const ZoneOutputs& out, int offset, int count, OutputGate& gate, bool open)
{
    gate.process (buffer.getArrayOfWritePointers(), outputs, count, open);

    const auto route = [&] (float* destination, int channel)
    {
        if (destination != nullptr)
            juce::FloatVectorOperations::copy (destination + offset, buffer.getReadPointer (channel - 1), count);
    };
    if (subChannel > 0)
        route (out.sub, subChannel);
    const auto tops = std::min (out.tops.size(), topChannels.size());
    for (size_t k = 0; k < tops; ++k)
        route (out.tops[k], topChannels[k]);
}

const float* ZoneDecoder::port (int index) const
{
    const bool hasSub = subChannel > 0;
    const int channel = hasSub && index == 0 ? subChannel : topChannels[(size_t) (index - (hasSub ? 1 : 0))];
    return buffer.getReadPointer (channel - 1);
}

int PortMeters::addGroup (int width)
{
    const int group = (int) groups.size();
    groups.push_back (std::make_unique<Meters>());
    for (int side = 0; side < std::clamp (width, 1, 2); ++side)
        portSides.emplace_back (group, side);
    return group;
}

void PortMeters::prepare (double sampleRate)
{
    for (auto& meters : groups)
        meters->prepare (sampleRate);
}

void PortMeters::push (int group, const float* left, const float* right, int count)
{
    const float* pair[2] { left, right != nullptr ? right : left };
    groups[(size_t) group]->push (pair, count);
}

float PortMeters::takePeak (int port)
{
    const auto [group, side] = portSides[(size_t) port];
    return groups[(size_t) group]->takePeak (side);
}

float PortMeters::rms (int port) const
{
    const auto [group, side] = portSides[(size_t) port];
    return groups[(size_t) group]->rms (side);
}

HeadphoneDecoder::HeadphoneDecoder (const juce::var& parameters, const juce::String& name)
{
    decoder = engine::createNode (IemKind::binauralDecoder, engine::busChannels, 2, name);
    engine::require (decoder->setParameters (parameters), name + ".parameters.json");
}

void HeadphoneDecoder::prepare (double sampleRate, int blockSize)
{
    buffer.setSize (engine::busChannels, blockSize);
    decoder->prepare (sampleRate, blockSize);
}

void HeadphoneDecoder::process (const juce::AudioBuffer<float>& bus16, int count)
{
    for (int ch = 0; ch < engine::busChannels; ++ch)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), bus16.getReadPointer (ch), count);
    engine::runNode (*decoder, buffer, count);
}
}
