// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "IemNode.h"
#include "SpatialConfig.h"

namespace a3
{
constexpr int numSpatialStrips = 4;

struct SpatialControls { std::atomic<float> azimuth[numSpatialStrips] {}, elevation[numSpatialStrips] {}; };   // degrees, already clamped
struct StripTaps { const float* moving[2]; const float* steady[2]; };
struct ZoneOutputs { float* sub = nullptr; std::vector<float*> tops; };   // tops.size() == layout tops; sub only when the layout has one
struct SpatialOutputs { float* rec[2] {}; ZoneOutputs main, booth; };

// The four strips' taps into IEM's MultiEncoder (8 sources, order 3), the
// 16-channel bus into BinauralDecoder (rec) and one SimpleDecoder per zone:
//
//   source i  = sum of config.sources[i]           (the template's pin masks)
//   bus       = MultiEncoder (sources), azimuth/elevation 0..3 from SpatialControls
//   rec       = BinauralDecoder (bus)
//   zone      = SimpleDecoder (bus) * fixedGainDb, routed sub/tops by the layout
class SpatialBus
{
public:
    explicit SpatialBus (const SpatialConfig&);    // builds and configures the four nodes (allocates); throws std::runtime_error when IEM refuses
    ~SpatialBus();

    // The constructor without the exception: nullptr and the reason in `error` when IEM refuses the config.
    static std::unique_ptr<SpatialBus> create (const SpatialConfig&, juce::String& error);

    void prepare (double sampleRate, int blockSize);   // allocates; warm-up
    int preparedBlockSize() const;

    // Any numSamples >= 0: calls longer than the prepared block are processed in prepared-size chunks.
    void process (const std::array<StripTaps, numSpatialStrips>& in, const SpatialOutputs& out, int numSamples,
                  const SpatialControls& positions);

    // The encoder's 16 channels of the last chunk processed (its first numSamples
    // of that chunk); for tests, which compare the bus without the decoders' memory.
    const juce::AudioBuffer<float>& lastBusChunk() const { return bus; }

private:
    struct Zone
    {
        std::unique_ptr<IemNode> decoder;
        juce::AudioBuffer<float> buffer;
        float fixedGain = 1.0f;
        int subChannel = 0;
        std::vector<int> topChannels;
    };

    void buildEncoder (const SpatialConfig&);
    void buildRec (const SpatialConfig&);
    static Zone buildZone (const ZoneConfig&, const char* name);
    void processChunk (const std::array<StripTaps, numSpatialStrips>& in, const SpatialOutputs& out, int offset,
                       int count, const SpatialControls& positions);
    void sumSources (const std::array<StripTaps, numSpatialStrips>& in, int offset, int count);
    void writePositions (const SpatialControls& positions);
    void decodeZone (Zone&, const ZoneOutputs&, int offset, int count);
    void decodeRec (float* const* rec, int offset, int count);
    static void runNode (IemNode&, juce::AudioBuffer<float>& buffer, int count);

    std::array<std::vector<EncoderInput>, numEncoderSources> sources;
    std::unique_ptr<IemNode> encoder, rec;
    std::array<std::atomic<float>*, numSpatialStrips> azimuth {}, elevation {};
    std::array<juce::String, numSpatialStrips> azimuthIds, elevationIds;
    juce::AudioBuffer<float> bus, recBuffer;
    Zone main, booth;
    int blockSize = 0;
};
}
