// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The digital loop the engine measures its round trip over (RoundTripProbe.h): device channels that are
// reserved for it, so no route of the I/O map may use them.
#pragma once
#include <juce_core/juce_core.h>
#include <optional>
#include <string>

namespace a3app
{
// engine.json io.roundTripLoop: {"output": 11, "input": 11}. Device channels, 1-based as the I/O page shows them.
// The probe plays on `output` and records `input`, and only those two are reserved: one channel a side
// (2026-10-30; until then the S/PDIF pair, "channels": 2, which a file may still say and the load ignores). A file
// without the key gets this default (the rig's Scarlett 18i20, S/PDIF out 11 patched to S/PDIF in 11); null is no loop.
struct RoundTripLoop
{
    int output = 11, input = 11;
    bool operator== (const RoundTripLoop& other) const { return output == other.output && input == other.input; }
};

// The converters' share of the analog chain (analog in -> AD -> engine -> DA -> analog out): the analog loop's
// delay minus the digital loop's, both measured in one run, in samples at `sampleRate`, on `measuredOn` (YYYY-MM-DD).
struct ConverterShare
{
    int samples = 0;
    double sampleRate = 0.0;
    juce::String measuredOn;
    double ms() const { return sampleRate > 0.0 ? 1000.0 * samples / sampleRate : 0.0; }
    bool operator== (const ConverterShare& other) const
    {
        return samples == other.samples && sampleRate == other.sampleRate && measuredOn == other.measuredOn;
    }
};

// engine.json io.analogLoop: {"output": 6, "input": 8} and, once measured, "converterSamples", "sampleRate",
// "measuredOn". The pair the user patches a temporary cable between for a one-time measurement, chosen on the
// I/O page (0: that side not chosen yet). Optional; it is never run on its own, only on request.
struct AnalogLoop
{
    int output = 0, input = 0;
    std::optional<ConverterShare> converters;
    bool operator== (const AnalogLoop& other) const
    {
        return output == other.output && input == other.input && converters == other.converters;
    }
};

// The engine's own JACK ports of the loop. Not engine ports of the I/O map: the backend registers and links them.
inline const std::string loopOutputPort = "roundtrip_out", loopInputPort = "roundtrip_in";
inline const std::string analogOutputPort = "analogtest_out", analogInputPort = "analogtest_in";

// The device channel (1-based) a JACK port name stands for: system:playback_N / system:capture_N, and PipeWire's
// pro-audio names <node>.pro-output-*:playback_AUXn / <node>.pro-input-*:capture_AUXn (AUX0 is channel 1).
struct PeerChannel
{
    bool capture = false;
    int channel = 0;
};
std::optional<PeerChannel> deviceChannelOfPeer (const std::string& port);

// Whether the loop reserves device channel `channelOneBased` on the input (capture) or output (playback) side.
bool loopHolds (const std::optional<RoundTripLoop>&, bool input, int channelOneBased);
// Whether a JACK port name is one of the loop's channels.
bool isLoopPeer (const std::optional<RoundTripLoop>&, const std::string& port);
// "output 11" / "input 11" (the reserved channel of one side).
juce::String loopRangeText (const RoundTripLoop&, bool input);
}
