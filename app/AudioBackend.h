// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ConfigFile.h"
#include "EngineHost.h"
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
};

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
};
}
