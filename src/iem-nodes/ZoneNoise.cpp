// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ZoneNoise.h"
#include <JuceHeader.h>
#include "efficientSHvanilla.h"
#include "ambisonicTools.h"
#include "NoiseBurst.h"
#include "AmbisonicNoiseBurst.h"

namespace a3
{
namespace
{
constexpr int busChannels = 16;
constexpr int busOrder = 3;
// IEM's processBuffer takes the buffer by value: a view over channel pointers is copied without
// allocating (JUCE shares external data and its preallocated channel table holds up to 31).
constexpr int maxViewChannels = 31;
}

struct ZoneNoise::Bursts
{
    NoiseBurst channel;
    AmbisonicNoiseBurst direction;
};

ZoneNoise::ZoneNoise (bool inputIsSn3d) : bursts (std::make_unique<Bursts>())
{
    bursts->direction.setOrder (busOrder);
    bursts->direction.setNormalization (inputIsSn3d);
}

ZoneNoise::~ZoneNoise() = default;

void ZoneNoise::prepare (double sampleRate, int blockSize)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, (juce::uint32) busChannels };
    bursts->channel.prepare (spec);
    bursts->direction.prepare (spec);
}

void ZoneNoise::requestChannel (int channel) { pendingChannel.store (channel, std::memory_order_release); }

void ZoneNoise::requestDirection (float azimuthDegrees, float elevationDegrees)
{
    if (! std::isfinite (azimuthDegrees) || ! std::isfinite (elevationDegrees))
        return;   // NaN would stay in the zone decoder's filters until the next rebuild
    pendingAzimuth.store (azimuthDegrees, std::memory_order_relaxed);
    pendingElevation.store (elevationDegrees, std::memory_order_relaxed);
    pendingDirection.store (true, std::memory_order_release);
}

void ZoneNoise::addToInput (float* const* bus16, int count)
{
    if (pendingDirection.exchange (false, std::memory_order_acquire))
        bursts->direction.play (pendingAzimuth.load (std::memory_order_relaxed), pendingElevation.load (std::memory_order_relaxed));
    if (! bursts->direction.isActive())
        return;
    bursts->direction.processBuffer (juce::AudioBuffer<float> (bus16, busChannels, count));
}

void ZoneNoise::addToOutputs (float* const* outputs, int numOutputs, int count)
{
    if (const int channel = pendingChannel.exchange (0, std::memory_order_acquire); channel > 0)
        bursts->channel.setChannel (channel);
    if (! bursts->channel.isActive())
        return;
    jassert (numOutputs <= maxViewChannels);
    bursts->channel.processBuffer (juce::AudioBuffer<float> (outputs, juce::jmin (numOutputs, maxViewChannels), count));
}
}
