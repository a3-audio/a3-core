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
// engine.json io.roundTripLoop: {"output": 11, "input": 11, "channels": 2}. Device channels, 1-based as the I/O
// page shows them. The probe plays on `output` and records `input`; `channels` consecutive channels from each
// are reserved (the S/PDIF pair is one cable). A file without the key gets this default (the rig's
// Scarlett 18i20, S/PDIF out 11-12 patched to S/PDIF in 11-12); null is no loop.
struct RoundTripLoop
{
    int output = 11, input = 11, channels = 2;
    bool operator== (const RoundTripLoop& other) const
    {
        return output == other.output && input == other.input && channels == other.channels;
    }
};

// The engine's own JACK ports of the loop. Not engine ports of the I/O map: the backend registers and links them.
inline const std::string loopOutputPort = "roundtrip_out", loopInputPort = "roundtrip_in";

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
// "output 11-12" / "input 11" (the reserved range of one side).
juce::String loopRangeText (const RoundTripLoop&, bool input);
}
