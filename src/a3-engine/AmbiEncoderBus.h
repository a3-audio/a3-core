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
struct StereoIn { const float* l = nullptr; const float* r = nullptr; };   // null = silence

// The analog channels' taps into one 16-channel order-3 bus (R3):
//
//   source c (0..A-1)    = moving[c].l + moving[c].r          at azimuth[c]/elevation[c]
//   bed k    (A..A+3)    = sum over c of steady[c].side_k + ret.side_k, at bedFrom(config)[k]
//   bus                  = sum of all encoders' outputs
//
// One MultiEncoder per 64 inputs: encoder 0 carries the first 60 moving
// sources and the bed, every further encoder the next 64 moving sources.
// Decks never pass through here.
class AmbiEncoderBus
{
public:
    // analogChannels may be 0 (bed and return only). nullptr and the reason in `error` when it cannot be built.
    static std::unique_ptr<AmbiEncoderBus> create (int analogChannels, const SpatialConfig&, juce::String& error);
    ~AmbiEncoderBus();

    void prepare (double sampleRate, int blockSize);   // allocates; warm-up

    // moving and steady: one entry per analog source (missing entries are silence);
    // azimuth/elevation: analogChannels values each, degrees, in source order.
    // bus16 gets channels 0..15 for samples 0..numSamples-1; calls longer than the
    // prepared block are processed in prepared-size chunks.
    void process (const std::vector<StereoIn>& moving, const std::vector<StereoIn>& steady, StereoIn ret,
                  juce::AudioBuffer<float>& bus16, int numSamples,
                  const std::atomic<float>* azimuth, const std::atomic<float>* elevation);

    int encoderCount() const;

private:
    struct Encoder;

    AmbiEncoderBus (int analogChannels, const SpatialConfig&);   // throws std::runtime_error

    void processChunk (const std::vector<StereoIn>& moving, const std::vector<StereoIn>& steady, StereoIn ret,
                       juce::AudioBuffer<float>& bus16, int offset, int count,
                       const std::atomic<float>* azimuth, const std::atomic<float>* elevation);
    void sumSides (const std::vector<StereoIn>& steady, StereoIn ret, int offset, int count);
    void fillInputs (Encoder&, const std::vector<StereoIn>& moving, int offset, int count);
    static void writePositions (Encoder&, const std::atomic<float>* azimuth, const std::atomic<float>* elevation);

    int analogChannels = 0;
    std::array<BedSource, numBedSources> bed {};
    std::vector<std::unique_ptr<Encoder>> encoders;
    juce::AudioBuffer<float> sideSums;   // steady + return, per side
    int blockSize = 0;
};
}
