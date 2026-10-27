// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AmbiEncoderBus.h"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace a3
{
namespace
{
constexpr int busChannels = 16;   // order 3
constexpr int maxEncoderInputs = 64;   // IEM MultiEncoder's limit
constexpr int movingOnFirstEncoder = maxEncoderInputs - numBedSources;
constexpr int sides = 2;

void require (const juce::Result& result, const juce::String& what)
{
    if (result.failed())
        throw std::runtime_error ((what + ": " + result.getErrorMessage()).toStdString());
}

juce::String indexed (const char* name, int index)
{
    return name + juce::String (index);
}

// The bed must take one side only, or a single side sum could not feed it.
void requireOneSidePerBedSource (const SpatialConfig& config)
{
    for (int k = 0; k < numBedSources; ++k)
    {
        const auto& inputs = config.sources[(size_t) (firstBedSourceInFile + k)];
        const bool oneSide = ! inputs.empty()
                             && std::all_of (inputs.begin(), inputs.end(),
                                             [&] (const EncoderInput& in) { return in.side == inputs.front().side; });
        if (! oneSide)
            throw std::runtime_error ("encoder.inputs.json: bed source " + std::to_string (firstBedSourceInFile + k + 1)
                                      + " must take exactly one side");
    }
}

// The template's encoder parameters with the sources re-indexed: every input at
// 0 dB and the origin, the bed (when carried) right after the moving sources.
juce::var encoderParameters (const SpatialConfig& config, int numMoving, const std::array<BedSource, numBedSources>* bed)
{
    auto parameters = config.encoderParameters.clone();
    auto* object = parameters.getDynamicObject();
    if (object == nullptr)
        throw std::runtime_error ("encoder.parameters.json: not an object");

    for (int i = 0; i < maxEncoderInputs; ++i)
        for (const char* name : { "azimuth", "elevation", "gain" })
            object->setProperty (indexed (name, i), 0.0f);
    object->setProperty ("inputSetting", numMoving + (bed != nullptr ? numBedSources : 0));
    if (bed == nullptr)
        return parameters;

    for (int k = 0; k < numBedSources; ++k)
    {
        const auto& source = (*bed)[(size_t) k];
        object->setProperty (indexed ("azimuth", numMoving + k), source.azimuth);
        object->setProperty (indexed ("elevation", numMoving + k), source.elevation);
        object->setProperty (indexed ("gain", numMoving + k), source.gainDb);
    }
    return parameters;
}
}

struct AmbiEncoderBus::Encoder
{
    std::unique_ptr<IemNode> node;
    int firstMoving = 0, numMoving = 0;
    bool carriesBed = false;
    std::vector<std::atomic<float>*> azimuth, elevation;
    std::vector<juce::String> azimuthIds, elevationIds;
    juce::AudioBuffer<float> buffer;
    // Refers to `buffer` for one chunk. Built once in prepare: above 31 channels
    // JUCE allocates a new channel table for every fresh view.
    juce::AudioBuffer<float> view;

    int numInputs() const { return numMoving + (carriesBed ? numBedSources : 0); }
    int numChannels() const { return juce::jmax (numInputs(), busChannels); }
};

AmbiEncoderBus::AmbiEncoderBus (int count, const SpatialConfig& config) : analogChannels (count), bed (bedFrom (config))
{
    if (count < 0)
        throw std::runtime_error ("analog channel count must not be negative: " + std::to_string (count));
    requireOneSidePerBedSource (config);

    int next = 0;
    do
    {
        auto encoder = std::make_unique<Encoder>();
        encoder->carriesBed = encoders.empty();
        encoder->firstMoving = next;
        encoder->numMoving = juce::jmin (count - next, encoder->carriesBed ? movingOnFirstEncoder : maxEncoderInputs);
        const auto name = "encoder " + juce::String ((int) encoders.size());

        encoder->node = IemNode::create (IemKind::multiEncoder, encoder->numInputs(), busChannels);
        if (encoder->node == nullptr)
            throw std::runtime_error ((name + ": IEM refused " + juce::String (encoder->numInputs()) + " inputs").toStdString());
        require (encoder->node->setParameters (encoderParameters (config, encoder->numMoving, encoder->carriesBed ? &bed : nullptr)),
                 name + " (encoder.parameters.json)");

        for (int i = 0; i < encoder->numMoving; ++i)
        {
            encoder->azimuthIds.push_back (indexed ("azimuth", i));
            encoder->elevationIds.push_back (indexed ("elevation", i));
            encoder->azimuth.push_back (encoder->node->rawParameter (encoder->azimuthIds.back()));
            encoder->elevation.push_back (encoder->node->rawParameter (encoder->elevationIds.back()));
            if (encoder->azimuth.back() == nullptr || encoder->elevation.back() == nullptr)
                throw std::runtime_error ((name + ": no azimuth/elevation parameter for input " + juce::String (i)).toStdString());
        }
        next += encoder->numMoving;
        encoders.push_back (std::move (encoder));
    } while (next < count);
}

AmbiEncoderBus::~AmbiEncoderBus() = default;

std::unique_ptr<AmbiEncoderBus> AmbiEncoderBus::create (int count, const SpatialConfig& config, juce::String& error)
{
    try
    {
        return std::unique_ptr<AmbiEncoderBus> (new AmbiEncoderBus (count, config));
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return nullptr;
    }
}

int AmbiEncoderBus::encoderCount() const
{
    return (int) encoders.size();
}

void AmbiEncoderBus::prepare (double sampleRate, int newBlockSize)
{
    blockSize = newBlockSize;
    sideSums.setSize (sides, blockSize);
    for (auto& encoder : encoders)
    {
        encoder->buffer.setSize (encoder->numChannels(), blockSize);
        encoder->buffer.clear();
        encoder->view.setDataToReferTo (encoder->buffer.getArrayOfWritePointers(), encoder->numChannels(), blockSize);
        encoder->node->prepare (sampleRate, blockSize);
    }
}

void AmbiEncoderBus::process (const std::vector<StereoIn>& moving, const std::vector<StereoIn>& steady, StereoIn ret,
                              juce::AudioBuffer<float>& bus16, int numSamples,
                              const std::atomic<float>* azimuth, const std::atomic<float>* elevation)
{
    jassert (blockSize > 0);
    jassert (bus16.getNumChannels() >= busChannels && bus16.getNumSamples() >= numSamples);
    if (blockSize <= 0)
        return;
    juce::ScopedNoDenormals noDenormals;
    for (int offset = 0; offset < numSamples; offset += blockSize)
        processChunk (moving, steady, ret, bus16, offset, juce::jmin (blockSize, numSamples - offset), azimuth, elevation);
}

void AmbiEncoderBus::processChunk (const std::vector<StereoIn>& moving, const std::vector<StereoIn>& steady, StereoIn ret,
                                   juce::AudioBuffer<float>& bus16, int offset, int count,
                                   const std::atomic<float>* azimuth, const std::atomic<float>* elevation)
{
    sumSides (steady, ret, offset, count);
    for (size_t e = 0; e < encoders.size(); ++e)
    {
        auto& encoder = *encoders[e];
        fillInputs (encoder, moving, offset, count);
        if (encoder.carriesBed)
            for (int k = 0; k < numBedSources; ++k)
                encoder.buffer.copyFrom (encoder.numMoving + k, 0, sideSums, bed[(size_t) k].side, 0, count);
        writePositions (encoder, azimuth, elevation);

        encoder.view.setDataToReferTo (encoder.buffer.getArrayOfWritePointers(), encoder.numChannels(), count);
        encoder.node->process (encoder.view);

        for (int ch = 0; ch < busChannels; ++ch)
        {
            if (e == 0)
                bus16.copyFrom (ch, offset, encoder.buffer, ch, 0, count);
            else
                bus16.addFrom (ch, offset, encoder.buffer, ch, 0, count);
        }
    }
}

void AmbiEncoderBus::sumSides (const std::vector<StereoIn>& steady, StereoIn ret, int offset, int count)
{
    const auto add = [&] (float* sum, const float* tap)
    {
        if (tap != nullptr)
            juce::FloatVectorOperations::add (sum, tap + offset, count);
    };
    const auto channels = juce::jmin ((size_t) analogChannels, steady.size());
    for (int side = 0; side < sides; ++side)
    {
        auto* sum = sideSums.getWritePointer (side);
        juce::FloatVectorOperations::clear (sum, count);
        for (size_t c = 0; c < channels; ++c)
            add (sum, side == 0 ? steady[c].l : steady[c].r);
        add (sum, side == 0 ? ret.l : ret.r);
    }
}

// F6: a moving source is its strip's left and right, added plainly.
void AmbiEncoderBus::fillInputs (Encoder& encoder, const std::vector<StereoIn>& moving, int offset, int count)
{
    for (int i = 0; i < encoder.numMoving; ++i)
    {
        auto* input = encoder.buffer.getWritePointer (i);
        juce::FloatVectorOperations::clear (input, count);
        const auto source = (size_t) (encoder.firstMoving + i);
        if (source >= moving.size())
            continue;
        for (const float* tap : { moving[source].l, moving[source].r })
            if (tap != nullptr)
                juce::FloatVectorOperations::add (input, tap + offset, count);
    }
}

// Raw APVTS writes as SpatialBus does: no listener, no lock. MultiEncoder reads
// them once per block and ramps its coefficients across that block.
void AmbiEncoderBus::writePositions (Encoder& encoder, const std::atomic<float>* azimuth, const std::atomic<float>* elevation)
{
    if (azimuth == nullptr || elevation == nullptr)
        return;
    for (size_t i = 0; i < (size_t) encoder.numMoving; ++i)
    {
        const auto source = (size_t) encoder.firstMoving + i;
        encoder.azimuth[i]->store (encoder.node->legalValue (encoder.azimuthIds[i], azimuth[source].load (std::memory_order_relaxed)),
                                   std::memory_order_relaxed);
        encoder.elevation[i]->store (encoder.node->legalValue (encoder.elevationIds[i], elevation[source].load (std::memory_order_relaxed)),
                                     std::memory_order_relaxed);
    }
}
}
