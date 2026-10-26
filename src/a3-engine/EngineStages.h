// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <utility>
#include <vector>
#include "Meters.h"
#include "IemNode.h"
#include "OutputGate.h"
#include "PurestGain.h"
#include "SilenceFade.h"
#include "SpatialBus.h"
#include "SpatialConfig.h"

// The small stages the engine graph is built from; none allocates in process.
namespace a3
{
// MASTER, BOOTH, RETURN, PHONES: a PurestGain at slopeVolume (one gain for all
// channels, as REAPER's pair instances) and, behind it, silence at 0 (R18).
class GainStage
{
public:
    void prepare (double sampleRate);
    void set (float control);   // wire value 0..1, latched on PurestGain's grid
    void process (float* const* io, int numSamples, int numChannels);

private:
    PurestGain gain;
    SilenceFade silence;
};

// A send weight (aux, cue, mix): moves linearly towards its target with a
// fixed slope, 0 -> 1 in 10 ms (R6). The first target is taken without a ramp.
class WeightRamp
{
public:
    void prepare (double sampleRate);
    void setTarget (float weight);
    void render (float* curve, int numSamples);     // the weight at each sample
    bool renderedSilence() const { return silence; }   // the last render was all zeros

private:
    float step = 1.0f, current = 0.0f, target = 0.0f;
    bool started = false, silence = true;
};

// IEM's SimpleDecoder for one zone, its level stage (MASTER or BOOTH) and its gate:
//   outputs = route (gate (stage (SimpleDecoder (bus16))))
// The stage sits after the decoder so that 0 is exact silence at once: in front
// of it the crossover filters would ring out. PurestGain is linear, so the
// place does not change the level. ZoneConfig::fixedGainDb is not applied (R1):
// the stage is the zone's knob.
class ZoneDecoder
{
public:
    ZoneDecoder (const ZoneConfig&, const juce::String& name);   // throws std::runtime_error
    void prepare (double sampleRate, int blockSize);
    void setLevel (float control) { stage.set (control); }
    void process (const juce::AudioBuffer<float>& bus16, const ZoneOutputs& out, int offset, int count, OutputGate&, bool open);
    // The zone's ports in order (sub when the layout has one, then the tops) and what
    // each carried in the last process call.
    int ports() const { return (subChannel > 0 ? 1 : 0) + (int) topChannels.size(); }
    const float* port (int index) const;

private:
    std::unique_ptr<IemNode> decoder;
    GainStage stage;
    juce::AudioBuffer<float> buffer;
    int outputs = 0;
    int subChannel = 0;
    std::vector<int> topChannels;
};

// IEM's BinauralDecoder (16 in, 2 out); out[0..1] are count samples long, nullptr = not written.
class HeadphoneDecoder
{
public:
    HeadphoneDecoder (const juce::var& parameters, const juce::String& name);   // throws std::runtime_error
    void prepare (double sampleRate, int blockSize);
    void process (const juce::AudioBuffer<float>& bus16, int count);
    float* side (int index) { return buffer.getWritePointer (index); }   // the last result, count samples

private:
    std::unique_ptr<IemNode> decoder;
    juce::AudioBuffer<float> buffer;
};

// Meters (beat-analyzer's ballistics) for output ports in groups of one or two:
// pushed on the audio thread, read by one reader elsewhere (the read takes the peak).
class PortMeters
{
public:
    int addGroup (int width);                 // 1 or 2 ports; returns the group; allocates
    void prepare (double sampleRate);
    void push (int group, const float* left, const float* right, int count);   // right unused for width 1
    int ports() const { return (int) portSides.size(); }
    float takePeak (int port);
    float rms (int port) const;

private:
    std::vector<std::unique_ptr<Meters>> groups;
    std::vector<std::pair<int, int>> portSides;   // port -> group, side
};

namespace engine
{
constexpr int busChannels = 16;   // order 3
std::unique_ptr<IemNode> createNode (IemKind, int numInputs, int numOutputs, const juce::String& what);   // throws
void require (const juce::Result&, const juce::String& what);                                           // throws
void runNode (IemNode&, juce::AudioBuffer<float>& buffer, int count);   // count <= the buffer's samples; at most 31 channels
}
}
