// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialBus.h"
#include <stdexcept>

namespace a3
{
namespace
{
constexpr int busChannels = 16;   // order 3
constexpr int recOutputs = 2;
// The per-chunk buffer views use JUCE's preallocated channel table; a buffer
// with more channels than that would allocate on the audio thread.
constexpr int maxViewChannels = 31;

void require (const juce::Result& result, const juce::String& what)
{
    if (result.failed())
        throw std::runtime_error ((what + ": " + result.getErrorMessage()).toStdString());
}

std::unique_ptr<IemNode> createNode (IemKind kind, int numInputs, int numOutputs, const juce::String& what)
{
    auto node = IemNode::create (kind, numInputs, numOutputs);
    if (node == nullptr)
        throw std::runtime_error ((what + ": IEM refused " + juce::String (numInputs) + " in / "
                                   + juce::String (numOutputs) + " out").toStdString());
    return node;
}

void copyChannels (const juce::AudioBuffer<float>& from, juce::AudioBuffer<float>& to, int count)
{
    for (int ch = 0; ch < to.getNumChannels(); ++ch)
    {
        if (ch < from.getNumChannels())
            juce::FloatVectorOperations::copy (to.getWritePointer (ch), from.getReadPointer (ch), count);
        else
            juce::FloatVectorOperations::clear (to.getWritePointer (ch), count);
    }
}
}

SpatialBus::SpatialBus (const SpatialConfig& config) : sources (config.sources)
{
    buildEncoder (config);
    buildRec (config);
    main = buildZone (config.main, "main");
    booth = buildZone (config.booth, "booth");
}

SpatialBus::~SpatialBus() = default;

std::unique_ptr<SpatialBus> SpatialBus::create (const SpatialConfig& config, juce::String& error)
{
    try
    {
        return std::make_unique<SpatialBus> (config);
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return nullptr;
    }
}

void SpatialBus::buildEncoder (const SpatialConfig& config)
{
    encoder = createNode (IemKind::multiEncoder, numEncoderSources, busChannels, "encoder");
    require (encoder->setParameters (config.encoderParameters), "encoder.parameters.json");
    for (int i = 0; i < numSpatialStrips; ++i)
    {
        azimuthIds[(size_t) i] = "azimuth" + juce::String (i);
        elevationIds[(size_t) i] = "elevation" + juce::String (i);
        azimuth[(size_t) i] = encoder->rawParameter (azimuthIds[(size_t) i]);
        elevation[(size_t) i] = encoder->rawParameter (elevationIds[(size_t) i]);
        if (azimuth[(size_t) i] == nullptr || elevation[(size_t) i] == nullptr)
            throw std::runtime_error ("encoder: no azimuth/elevation parameter for source " + std::to_string (i));
    }
}

void SpatialBus::buildRec (const SpatialConfig& config)
{
    rec = createNode (IemKind::binauralDecoder, busChannels, recOutputs, "rec");
    require (rec->setParameters (config.recParameters), "rec.parameters.json");
}

SpatialBus::Zone SpatialBus::buildZone (const ZoneConfig& config, const char* name)
{
    const auto outputs = config.layout.numOutputs();
    if (juce::jmax (busChannels, outputs) > maxViewChannels)
        throw std::runtime_error (std::string (name) + ": more than " + std::to_string (maxViewChannels)
                                  + " decoder channels are not supported");
    Zone zone;
    zone.decoder = createNode (IemKind::simpleDecoder, busChannels, outputs, name);
    require (zone.decoder->setParameters (config.parameters), juce::String (name) + ".parameters.json");
    require (zone.decoder->loadDecoderConfig (config.layout.decoderJson), juce::String (name) + ".decoder.json");
    zone.fixedGain = juce::Decibels::decibelsToGain (config.fixedGainDb);
    zone.subChannel = config.layout.subChannel;
    zone.topChannels = config.layout.topChannels;
    return zone;
}

void SpatialBus::prepare (double sampleRate, int newBlockSize)
{
    blockSize = newBlockSize;
    bus.setSize (busChannels, blockSize);
    recBuffer.setSize (busChannels, blockSize);
    encoder->prepare (sampleRate, blockSize);
    rec->prepare (sampleRate, blockSize);
    for (auto* zone : { &main, &booth })
    {
        zone->buffer.setSize (juce::jmax (busChannels, zone->decoder->processor().getTotalNumOutputChannels()), blockSize);
        zone->decoder->prepare (sampleRate, blockSize);
    }
}

int SpatialBus::preparedBlockSize() const
{
    return blockSize;
}

void SpatialBus::process (const std::array<StripTaps, numSpatialStrips>& in, const SpatialOutputs& out,
                          int numSamples, const SpatialControls& positions)
{
    jassert (blockSize > 0);
    if (blockSize <= 0)
        return;
    juce::ScopedNoDenormals noDenormals;
    for (int offset = 0; offset < numSamples; offset += blockSize)
        processChunk (in, out, offset, juce::jmin (blockSize, numSamples - offset), positions);
}

void SpatialBus::processChunk (const std::array<StripTaps, numSpatialStrips>& in, const SpatialOutputs& out,
                               int offset, int count, const SpatialControls& positions)
{
    sumSources (in, offset, count);
    writePositions (positions);
    runNode (*encoder, bus, count);
    decodeRec (out.rec, offset, count);
    decodeZone (main, out.main, offset, count);
    decodeZone (booth, out.booth, offset, count);
}

void SpatialBus::sumSources (const std::array<StripTaps, numSpatialStrips>& in, int offset, int count)
{
    for (int source = 0; source < numEncoderSources; ++source)
    {
        auto* sum = bus.getWritePointer (source);
        juce::FloatVectorOperations::clear (sum, count);
        for (const auto& entry : sources[(size_t) source])
        {
            const auto& strip = in[(size_t) entry.strip];
            const float* tap = (entry.tap == Tap::moving ? strip.moving : strip.steady)[entry.side];
            juce::FloatVectorOperations::addWithMultiply (sum, tap + offset, entry.gain, count);
        }
    }
}

// Raw APVTS writes: no listener, no lock. MultiEncoder reads them once per
// block and ramps its coefficients across that block.
void SpatialBus::writePositions (const SpatialControls& positions)
{
    for (size_t i = 0; i < (size_t) numSpatialStrips; ++i)
    {
        azimuth[i]->store (encoder->legalValue (azimuthIds[i], positions.azimuth[i].load (std::memory_order_relaxed)),
                           std::memory_order_relaxed);
        elevation[i]->store (encoder->legalValue (elevationIds[i], positions.elevation[i].load (std::memory_order_relaxed)),
                             std::memory_order_relaxed);
    }
}

void SpatialBus::runNode (IemNode& node, juce::AudioBuffer<float>& buffer, int count)
{
    juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
    node.process (chunk);
}

void SpatialBus::decodeRec (float* const* recOut, int offset, int count)
{
    copyChannels (bus, recBuffer, count);
    runNode (*rec, recBuffer, count);
    for (int side = 0; side < recOutputs; ++side)
        if (recOut[side] != nullptr)
            juce::FloatVectorOperations::copy (recOut[side] + offset, recBuffer.getReadPointer (side), count);
}

void SpatialBus::decodeZone (Zone& zone, const ZoneOutputs& out, int offset, int count)
{
    jassert ((out.sub != nullptr) == (zone.subChannel > 0));
    jassert (out.tops.size() == zone.topChannels.size());
    copyChannels (bus, zone.buffer, count);
    runNode (*zone.decoder, zone.buffer, count);

    const auto route = [&] (float* destination, int channel)
    {
        juce::FloatVectorOperations::copyWithMultiply (destination + offset, zone.buffer.getReadPointer (channel - 1),
                                                       zone.fixedGain, count);
    };
    if (zone.subChannel > 0 && out.sub != nullptr)
        route (out.sub, zone.subChannel);
    const auto tops = juce::jmin (out.tops.size(), zone.topChannels.size());
    for (size_t k = 0; k < tops; ++k)
        route (out.tops[k], zone.topChannels[k]);
}
}
