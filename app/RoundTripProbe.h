// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The measured round trip over a digital loop (the Scarlett's S/PDIF out patched to its S/PDIF in): a quiet
// test signal on the loop's output, its own input recorded, and the delay found by cross-correlation.
#pragma once
#include "RoundTripLoop.h"
#include <atomic>
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace a3app
{
// A maximum-length sequence of 2^order - 1 samples at +-amplitude (-20 dBFS by default): flat in spectrum, and
// its correlation with itself is one sharp peak, so a delay shows as exactly one lag.
std::vector<float> testSequence (int order = 13, float amplitude = 0.1f);

struct LoopDelay
{
    bool found = false;      // a clear peak: the signal came back
    int samples = 0;         // the lag of that peak, from writing the signal to reading it
    double clarity = 0.0;    // the peak against the largest correlation away from it
    double coherence = 0.0;  // the peak against both signals' energy at that lag (1: the same signal, unscaled noise aside)
};

// Where `sent` occurs in `recorded` (lags 0 .. recorded - sent samples). Found only when the peak is clearly
// above every other lag and the recording at that lag is mostly the signal: silence, noise, another signal on
// the input, or a loop longer than the window are "no loop found", never a number.
LoopDelay findLoopDelay (const std::vector<float>& sent, const std::vector<float>& recorded);

// One measurement at a time. start, finished and collect run on the control thread; process on the audio
// thread, where it neither allocates nor locks (both buffers are made in the constructor). The handover is
// one atomic state: the audio thread owns the recording from start() until it says finished.
class RoundTripProbe
{
public:
    // maxDelaySamples: the longest loop that can be found (16384: 0.37 s at 44.1 kHz).
    explicit RoundTripProbe (int maxDelaySamples = 16384);

    bool start();             // false while one runs or waits to be collected
    bool busy() const;        // started and not yet collected
    bool finished() const;    // the recording is complete
    // Once finished: the delay, and the probe is free again. nullopt before.
    std::optional<LoopDelay> collect();

    // Each block: reads the loop input before it writes the loop output (either may be null: silence / nowhere).
    // Writes the test signal while a measurement plays and zeros otherwise.
    void process (const float* loopIn, float* loopOut, int numSamples);

private:
    enum State : int { idle, armed, running, done };
    const std::vector<float> signal;
    std::vector<float> recording;
    std::atomic<int> state { idle };
    int position = 0;   // the audio thread's
};

// What the backend says of the measured round trip (BackendStatus, the control API, the AUDIO page).
struct RoundTripReport
{
    enum class State
    {
        notReported,   // an engine before the measurement
        noLoop,        // nothing to measure over: `reason`
        notMeasured,   // a loop, no result yet
        measured,      // `samples`, at `atMillis`
        noLoopFound    // the last run found no clear peak: `reason`
    };
    State state = State::notReported;
    bool measuring = false;   // a run is under way (the last result stays until it ends)
    int samples = 0;
    juce::int64 atMillis = 0;   // wall clock of the measurement (ms since 1970)
    juce::String reason;
};

// The control-thread side of one backend's measurement: once autoAfterSeconds after the first service(), and on
// request(). process() is the probe's (audio thread).
class RoundTripMeter
{
public:
    static constexpr double autoAfterSeconds = 2.0;   // the links are made and the graph has settled

    // nullopt: no loop configured. unavailable: why the loop's channels are not there ("" when they are).
    explicit RoundTripMeter (std::optional<RoundTripLoop>, juce::String unavailable = {});

    void request();
    void service (double nowSeconds, juce::int64 wallMillis);
    void process (const float* loopIn, float* loopOut, int numSamples) { probe.process (loopIn, loopOut, numSamples); }
    RoundTripReport report() const;
    bool hasLoop() const { return loop.has_value() && unavailable.isEmpty(); }

private:
    const std::optional<RoundTripLoop> loop;
    const juce::String unavailable;
    RoundTripProbe probe;
    RoundTripReport last;
    double firstService = -1.0;
    bool autoDone = false;
};
}
