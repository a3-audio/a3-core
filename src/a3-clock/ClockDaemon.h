// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The clock role's loop (ClockSteering.h says why): the profiler's lines in, one PHC reading and one
// steering update a second, the frequency out, the status file and the journal line. The hardware is
// behind interfaces, so the tests run the whole loop against simulated clocks and never touch
// /dev/ptp0; the Linux side is app/ClockRole.h.
//
// Fail safe: a PHC that cannot be read or a profiler that is gone or silent is holdover (nothing
// written, the last frequency held, the profiler started again); a write that fails is a fault. A PHC
// that is not the audio port's is never steered (the role refuses before it starts). ptp4l's port in
// any state but MASTER pauses the steering; a port state nobody can read is reported as unverified,
// so the window never shows a plain LOCKED for it. Nothing here can set the PHC's time: PhcDevice has
// no such call.
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
    virtual std::optional<double> maxAdjustmentPpm() { return std::nullopt; }   // the device's own limit
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

// ptp4l's port state on the audio port ("MASTER", "SLAVE", ...), or nullopt when it cannot be read.
class PtpPortState
{
public:
    virtual ~PtpPortState() = default;
    virtual std::optional<std::string> portState() = 0;
};

struct DaemonOptions
{
    std::string device;                  // the PHC (resolved from the audio port by the role)
    std::string interface;               // the audio port it belongs to
    bool observeOnly = false;            // measure and report, never write
    std::string refusal;                 // non-empty: never steer, a fault that says this
    std::optional<double> warmFrequencyPpm;   // the last locked frequency, used when the PHC reads 0 (a boot)
    double logSeconds = 10.0;            // one journal line this often
    double feedRestartSeconds = 2.0;     // a profiler that ended is started again after this
    double feedSilenceSeconds = 10.0;    // ... and one alive without a Scarlett record for this long, too
    double failedStartLogSeconds = 60.0; // a profiler that cannot be started is said at most this often
    double portCheckSeconds = 30.0;      // ptp4l's port state is asked this often
    double rememberSeconds = 60.0;       // a locked frequency is handed to `remember` this often
    double rangeMarginPpm = 20.0;        // the range stays this far inside the device's own limit
    SteeringConfig steering;             // its updateSeconds is the daemon's update period
    IntakeConfig intake;
};

class ClockDaemon
{
public:
    using StatusSink = std::function<void (const ClockStatus&)>;
    using Journal = std::function<void (const std::string&)>;
    using Remember = std::function<void (double frequencyPpm)>;

    ClockDaemon (PhcDevice&, ProfilerFeed&, PtpPortState*, DaemonOptions, StatusSink, Journal, Remember, double nowSeconds);

    // Called often (the role's timer, 20 Hz): drains the profiler, and once an update period has gone
    // by, measures, steers, publishes. `nowUnix` stamps the status file.
    void tick (double nowSeconds, double nowUnix);

    const ClockStatus& status() const { return current; }

private:
    void startSteering (double nowSeconds);
    void startFeed (double nowSeconds);
    void drainFeed (double nowSeconds);
    void line (std::string_view, double nowSeconds);
    void checkPort (double nowSeconds);
    std::optional<Measurement> measure();
    void steer (double nowSeconds, std::optional<Measurement>);
    void observe (double nowSeconds, std::optional<Measurement>);
    void publish (double nowSeconds, double nowUnix);
    void say (const std::string& text) { if (journal) journal (text); }

    PhcDevice& phc;
    ProfilerFeed& feed;
    PtpPortState* port;
    DaemonOptions options;
    StatusSink sink;
    Journal journal;
    Remember remember;
    ScarlettIntake intake;
    PhcHistory history;
    RateEstimate rate;
    std::optional<ClockSteering> steering;
    ClockStatus current;
    double nextUpdate, nextLog, nextPortCheck, nextRemember = 0.0;
    std::optional<double> feedRestartAt, failedStartSaidAt;
    double lastScarlettRecord;
    std::string lastOtherDriver;         // what the profiler showed instead of the Scarlett
    std::optional<double> lockedSince;
    Health lastHealth = Health::acquiring;
    bool phcProblemSaid = false, portPaused = false;
    std::uint64_t observedEpoch = 0;
    std::int64_t observedOffset = 0;
};
}
