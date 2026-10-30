// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The clock role's loop (ClockSteering.h says why): the profiler's lines in, one PHC reading and one
// steering update a second, the frequency out, the status file and the journal line. The hardware is
// behind two interfaces, so the tests run the whole loop against simulated clocks and never touch
// /dev/ptp0; the Linux side is LinuxClockDevices.h.
//
// Fail safe: a PHC that cannot be read or a profiler that is gone is holdover (nothing written, the last
// frequency held, the profiler started again); a write that fails is a fault. Nothing here can set the
// PHC's time: PhcDevice has no such call.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "ClockMeasure.h"
#include "ClockStatus.h"
#include "ClockSteering.h"

namespace a3clock
{
// The PTP hardware clock.
class PhcDevice
{
public:
    virtual ~PhcDevice() = default;
    virtual std::optional<PhcSample> read() = 0;            // the PHC at a CLOCK_MONOTONIC instant
    virtual std::optional<double> frequencyPpm() = 0;       // its frequency adjustment as it is now
    virtual bool setFrequencyPpm (double) = 0;              // ADJ_FREQUENCY; false when refused
    virtual std::string problem() const = 0;                // the last failure, in words
};

// The Scarlett's side: `pw-profiler -J` as lines.
class ProfilerFeed
{
public:
    virtual ~ProfilerFeed() = default;
    virtual bool start() = 0;
    // The whole lines that arrived since the last call, each handed to `line`. False once the feed
    // has ended (the daemon starts it again).
    virtual bool drain (const std::function<void (std::string_view)>& line) = 0;
};

struct DaemonOptions
{
    std::string device = "/dev/ptp0";
    bool observeOnly = false;            // measure and report, never write
    double logSeconds = 10.0;            // one journal line this often
    double feedRestartSeconds = 2.0;     // a profiler that ended is started again after this
    SteeringConfig steering;             // its updateSeconds is the daemon's update period
    IntakeConfig intake;
};

class ClockDaemon
{
public:
    using StatusSink = std::function<void (const ClockStatus&)>;
    using Journal = std::function<void (const std::string&)>;

    ClockDaemon (PhcDevice&, ProfilerFeed&, DaemonOptions, StatusSink, Journal, double nowSeconds);

    // Called often (the role's timer, 20 Hz): drains the profiler, and once an update period has gone
    // by, measures, steers, publishes. `nowUnix` stamps the status file.
    void tick (double nowSeconds, double nowUnix);

    const ClockStatus& status() const { return current; }

private:
    void drainFeed (double nowSeconds);
    std::optional<Measurement> measure();
    void steer (double nowSeconds, std::optional<Measurement>);
    void observe (std::optional<Measurement>);
    void publish (double nowSeconds, double nowUnix);
    void say (const std::string& line) { if (journal) journal (line); }

    PhcDevice& phc;
    ProfilerFeed& feed;
    DaemonOptions options;
    StatusSink sink;
    Journal journal;
    ScarlettIntake intake;
    PhcHistory history;
    RateEstimate rate;
    std::optional<ClockSteering> steering;
    ClockStatus current;
    double nextUpdate, nextLog;
    std::optional<double> feedRestartAt;
    std::optional<double> lockedSince;
    Health lastHealth = Health::acquiring;
    bool phcProblemSaid = false;
    std::uint64_t observedEpoch = 0;
    std::int64_t observedOffset = 0;
};
}
