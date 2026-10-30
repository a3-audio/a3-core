// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One clock for the A³ system: the Scarlett on the Core.
//
// Why (the maintainer's requirement, 2026-10-30): "core gets clock from scarlett. all devices must rely
// on it. no software clock at all. tight latency is mandatory for live setup. ... it should never
// break." Decks B and C crackled because the deck hosts ran on a second crystal: their PipeWire graphs
// follow PTP, and PTP was the Core's Intel I210 hardware clock (PHC, /dev/ptp0) running free, about
// 14 ppm away from the Scarlett. The Core's receivers resampled the decks all the time.
//
// What this does (stage 1 of .claude/notes/2026-10-30-one-clock-research.md): the clock role reads the
// Scarlett's sample clock out of band (PipeWire's profiler: position + delay against CLOCK_MONOTONIC,
// never a node in the graph, so it cannot cause an xrun) and phase-locks the PHC to it with
// clock_adjtime(ADJ_FREQUENCY). ptp4l on the Core is the grandmaster and runs free_running, so it never
// touches the PHC; the deck hosts' ptp4l follow it as before, and with that the Scarlett's rate. Once
// both ends share one clock, the Core's deck receivers run as plain FIFOs (resample.disable).
//
// The rules this file keeps, each a test in tests/core/ClockSteering.cpp:
//   - It only ever changes the PHC's frequency, never its time: no step, whatever happens.
//   - The frequency stays inside centre +- range and never moves faster than maxSlewPpmPerSecond, so
//     the deck hosts' PTP servos follow without an offset.
//   - Frequency first, then phase, and fast (the maintainer: the Scarlett's clock is there at once, so
//     the PTP clock has to follow at once): it measures the rate for about 5 s, writes it in one go,
//     then anchors the phase; locked within about 11 s. A warm start writes the last locked frequency
//     at the first update and checks it against the rate measured meanwhile. These acquisition writes
//     are the only ones not slew-limited; once tracking, every change is.
//   - Phase lock, not only frequency lock: the phase error is the FIFO fill walk of every receiver, so
//     it has to stay bounded forever, not just stop growing.
//   - No measurement: hold the last frequency (holdover), never guess.
//   - A Scarlett discontinuity (xrun, restart, replug) or a stepped PHC is re-anchored, keeping the
//     error it had: a jump is never slewed into the PTP clock. What counts as a step is bounded by the
//     drift the held frequency allows (1 ppm of the gap), never by the whole range.
//   - Fail safe: a write that fails, or an error that runs away after a lock, stops the steering for
//     good (fault) with the last frequency held. That is what a dead daemon would leave, and it is said.
//
// Plain C++, no allocation after construction, no JUCE: the daemon around it (ClockDaemon.h) does the
// reading and writing, and none of it is on an audio thread.
#pragma once

#include <cstdint>
#include <optional>

#include "ClockMeasure.h"

namespace a3clock
{
// The numbers against the receivers (6 ms FIFO for the decks, refilled below one 128-frame cycle,
// ~2.9 ms: about 3 ms of headroom): the loop gives up acquiring at 2 ms and calls a locked loop broken at 1 ms, both
// far inside it; the lock is 50 us. The range is +-40 ppm around the free run (the I210 allows 62.5;
// the daemon narrows it to the device's own limit minus a margin), the measured offset is 14.1 ppm.
enum class Health
{
    acquiring,   // anchored (or waiting for the first measurement), steering towards the lock
    locked,      // |error| below the lock threshold for lockUpdates updates in a row
    holdover,    // no measurement for holdoverAfterSeconds: the last frequency is held, nothing written
    fault,       // steering stopped for good (see reason); the last frequency is held
    observing    // --observe: measures and reports, never writes
};

const char* healthWord (Health);

struct SteeringConfig
{
    double updateSeconds = 1.0;            // how often a measurement comes (the slew limit is per update of this)
    double acquireTimeConstantSeconds = 20.0;   // the PI's time constant until locked: the residual of a
                                                // 5 s rate measurement is pulled in fast ...
    double timeConstantSeconds = 60.0;     // ... and once settled: quiet, the deck hosts' servos follow it
    int settleUpdates = 60;                // updates in a row inside the lock threshold that count as settled
    double errorSmoothingSeconds = 4.0;    // the error is low-passed this much before the PI (profiler jitter)
    double centreFrequencyPpm = 0.0;       // the frequency range's centre: the PHC running free
    double frequencyRangePpm = 40.0;       // |frequency - centre| never exceeds this
    double maxSlewPpmPerSecond = 0.1;      // the frequency never moves faster than this
    double lockThresholdNs = 50'000.0;     // |error| below this counts towards the lock
    int lockUpdates = 5;                   // ... for this many updates in a row
    double holdoverAfterSeconds = 3.0;     // no measurement for this long: holdover
    int rateUpdates = 5;                   // measurements the rate is taken from (one a second)
    // 5 s of rate tell it to about 0.9 ppm rms. A PHC found within atRatePpm of the measured rate (a
    // restart on a locked clock) is not written at all; a warm start off by more than warmTolerancePpm
    // is corrected once (a needless correction only costs what a cold start costs).
    double atRatePpm = 3.0;
    double warmTolerancePpm = 1.5;
    double phcStepNs = 200'000.0;          // an error change above this (plus the held drift) is a step
    double heldDriftPpm = 1.0;             // what the held frequency may drift by in a gap
    double faultNs = 1'000'000.0;          // locked once, then |error| above this: fault
    double acquireLimitNs = 2'000'000.0;   // never locked and |error| above this: fault
};

// One phase measurement: PHC time minus Scarlett time at the same instant, in ns. The Scarlett side
// counts from its epoch's first sample; `epoch` changes at every Scarlett discontinuity.
struct Measurement
{
    std::uint64_t epoch = 0;
    std::int64_t phaseNs = 0;
};

struct Decision
{
    Health health = Health::acquiring;
    double frequencyPpm = 0.0;             // what the PHC runs at (after a write: the written value)
    std::optional<double> write;           // the frequency to write now; nullopt: write nothing
    bool jump = false;                     // an acquisition write (rate or warm start): not slew-limited
    double errorNs = 0.0;                  // the phase error after anchoring
    const char* reason = "";               // why the health is what it is (static text)
};

struct SteeringCounters
{
    std::int64_t updates = 0;
    std::int64_t corrections = 0;          // frequencies handed out to write
    std::int64_t reanchors = 0;            // Scarlett discontinuities re-anchored
    std::int64_t phcSteps = 0;             // PHC steps re-anchored
    std::int64_t holdovers = 0;            // times holdover was entered
    std::int64_t lockLosses = 0;           // locked -> acquiring
};

class ClockSteering
{
public:
    // `startFrequencyPpm` is the PHC's frequency as found. `warmFrequencyPpm`, the last locked one (a
    // state file), is written at the first update and replaces the wait for a rate measurement.
    ClockSteering (const SteeringConfig&, double startFrequencyPpm, double nowSeconds,
                   std::optional<double> warmFrequencyPpm = std::nullopt);

    // One update: a measurement, or nullopt when none could be taken (no Scarlett, no PHC reading).
    Decision update (double nowSeconds, std::optional<Measurement>);

    // The write the last decision asked for failed: fault, nothing is written again.
    void writeFailed (const char* reason);

    Health health() const { return state; }
    double frequencyPpm() const { return frequency; }
    double errorNs() const { return error; }
    const char* reason() const { return why; }
    const SteeringCounters& counters() const { return count; }
    const SteeringConfig& config() const { return cfg; }
    bool tracking() const { return stage == Stage::tracking; }   // anchored: the error means something

private:
    enum class Stage { measuringRate, tracking };

    Decision hold() const;
    Decision noMeasurement (double nowSeconds);
    Decision measureRate (double nowSeconds, const Measurement&);
    Decision jumpTo (double ppm, const char* reason);
    std::optional<double> warmCorrection (double nowSeconds);
    bool inRange (double ppm) const;
    double anchoredError (const Measurement&, double gapSeconds);
    double nextFrequency (double errorNs, double dtSeconds);
    void judge (double errorNs);

    SteeringConfig cfg;
    double gainFor (double timeConstant, bool proportional) const;
    double frequency, integral;
    double error = 0.0, smoothed = 0.0;
    double lastMeasurementAt;
    bool anchored = false, lockedOnce = false;
    std::uint64_t epoch = 0;
    std::int64_t offsetNs = 0;
    int inThreshold = 0;
    Stage stage = Stage::measuringRate;
    std::optional<double> pendingWarm;     // the warm start, written at the first update
    bool verifyingWarm = false;
    RateEstimate rate;
    std::int64_t rateOrigin = 0;
    Health state = Health::acquiring;
    const char* why = "waiting for the first measurement";
    SteeringCounters count;
};
}
