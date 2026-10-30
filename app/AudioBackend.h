// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AudioTiming.h"
#include "ConfigFile.h"
#include "EngineHost.h"
#include "RoundTripProbe.h"
#include <juce_core/juce_core.h>
#include <string>
#include <utility>
#include <vector>

namespace a3app
{
struct BackendStatus
{
    double sampleRate = 0;
    int blockSize = 0;
    juce::String rateSource;                 // where rate and block size are set
    float dspLoadPercent = 0;
    int xruns = 0;
    int bitDepth = 0;                        // JACK: 32 (float samples); a device: what it reports
    std::vector<juce::String> missingPeers;  // ports the I/O map names that the graph lacks now
    bool connecting = false;                 // false with --no-connect: the engine makes no link
    bool linksPlanned = false;               // the first link plan has run since the open; missingPeers means something only then
    // Samples between the converters and the engine's ports. JACK: the largest of the engine's ports' latency
    // ranges (capture on its inputs, playback on its outputs); a device: what the driver reports.
    int captureLatency = 0, playbackLatency = 0;
    // What the I/O page offers as a route. JACK: the physical capture and playback ports; a device: its
    // input and output channel counts.
    juce::StringArray capturePorts, playbackPorts;
    int deviceInputs = 0, deviceOutputs = 0;
    // JACK: the configured device's name for the I/O page ("Scarlett 18i20 USB", SystemDevice.h); empty elsewhere.
    juce::String deviceName;
    // The round trip measured over the I/O map's digital loop (RoundTripProbe.h).
    RoundTripReport roundTripMeasured;
    // The engine's block and periods on PipeWire (TimingControl.h); not available on a device.
    TimingReport timing;
};

// The input-to-output round trip as the backend reports its parts: an estimate (a driver's figures can be
// off, and a converter's own delay is in them only when the driver says so).
struct RoundTrip
{
    double sampleRate = 0;
    int captureSamples = 0, processingSamples = 0, playbackSamples = 0;   // processing: one period of the engine
    int totalSamples() const { return captureSamples + processingSamples + playbackSamples; }
    double ms (int samples) const { return sampleRate > 0 ? 1000.0 * samples / sampleRate : 0.0; }
    double totalMs() const { return ms (totalSamples()); }
};
RoundTrip estimatedRoundTrip (const BackendStatus&);
// "Round trip (estimated)   16.0 ms (768 samples) = capture 5.3 + processing 2.7 + playback 8.0 ms".
juce::String roundTripText (const RoundTrip&);
// The AUDIO page's line: "Round trip (measured, digital loop)   4.4 ms (211 samples), 14:05" when the loop was
// measured; else the estimate, "Round trip (estimated, <why not measured>)   ...".
juce::String roundTripText (const RoundTrip& estimate, const RoundTripReport& measured);
// The analog-chain line: the live digital loop plus the stored converters' share, or why it cannot say.
// "Analog chain   5.4 ms (measured: digital loop now + converters measured on 2026-10-30)".
juce::String analogChainText (const RoundTrip& estimate, const RoundTripReport& measured);

// The load STATUS shows: the larger of the backend's own figure (JACK: the whole graph; a device:
// JUCE's callback load) and the engine's own time for the last block against its period.
float combinedLoadPercent (float backendPercent, float engineBlockMs, int frames, double sampleRate);

// Where the engine's audio comes from and goes to (JACK on the rig, a JUCE device elsewhere).
// Every call except the audio callbacks it drives runs on the message thread.
class AudioBackend
{
public:
    virtual ~AudioBackend() = default;
    // Opens, prepares the host for the backend's rate and block size and starts calling
    // host.audioCallback. The host must outlive close().
    virtual juce::Result open (EngineHost&, const AppConfig&) = 0;
    virtual void close() = 0;
    virtual BackendStatus status() const = 0;
    // Every live link of the engine's own ports, foreign ones included (from, to).
    virtual std::vector<std::pair<std::string, std::string>> liveLinks() const = 0;
    // The message thread's share, from the app's control timer (~50 Hz): what the audio
    // thread's callbacks flagged (a new block size, a port that came or went) is acted on here.
    virtual void service (double /*nowSeconds*/) {}
    // Plays the test signal on the round-trip loop and measures (the result arrives in status()). Nothing while
    // one runs or without a loop.
    virtual void measureRoundTrip() {}
    // The analog chain: both loops in one run, only ever on this request (a temporary cable must be plugged in).
    virtual void measureAnalogChain() {}
    // A converter share measured since the last call, once (the engine stores it in its config).
    virtual std::optional<ConverterShare> takeNewConverterShare() { return std::nullopt; }
};
}
