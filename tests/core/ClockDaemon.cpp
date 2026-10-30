// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The clock role's loop against a simulated rig: a Scarlett that the profiler reports every 50 ms (with
// its driver record, jitter, bursts and a second ALSA driver if asked), a PHC that runs at its own rate
// plus what was written (a write may land one update late), and ptp4l's port state. Nothing here
// touches /dev/ptp0.
#include <gtest/gtest.h>

#include <cmath>
#include <deque>
#include <random>
#include <string>
#include <vector>

#include "ClockDaemon.h"

using namespace a3clock;

namespace
{
const std::string scarlettNode = "alsa_input.usb-Focusrite_Scarlett_18i20_USB_P9P0A77190B57F-00.pro-input-0";

// The rig as clocks against CLOCK_MONOTONIC: the Scarlett +5.5 ppm, the I210 free-running -8.6 ppm, so
// the Scarlett runs 14.1 ppm faster than the PHC (measured 2026-10-30).
struct World
{
    double now = 100.0;                       // MONOTONIC seconds
    double scarlettPpm = 5.5, phcNaturalPpm = -8.6;
    long double samples = 1'000'000.0L, phcNs = 1.7e18L;   // long double: a PHC in ns needs more than 53 bits
    std::int64_t xrun = 257;
    double delayJitterSamples = 0.0;          // position + delay jitter, samples rms
    std::mt19937 random { 11 };

    void advance (double seconds, double phcFrequencyPpm)
    {
        now += seconds;
        samples += 44100.0L * (1.0L + scarlettPpm * 1e-6L) * seconds;
        phcNs += 1e9L * (1.0L + (phcNaturalPpm + phcFrequencyPpm) * 1e-6L) * seconds;
    }
    std::int64_t monoNs() const { return (std::int64_t) std::llround (now * 1e9); }
    std::string clockRecord (int id = 375, long double position = -1.0L, std::int64_t xrunCount = -1)
    {
        std::normal_distribution<double> jitter (0.0, std::max (delayJitterSamples, 1e-9));
        const auto delay = delayJitterSamples > 0.0 ? 200 + (std::int64_t) std::lround (jitter (random)) : 0;
        return R"({ "type": "clock", "flags": 0, "id": )" + std::to_string (id) + R"(, "name": "api.alsa.1", "nsec": )"
               + std::to_string (monoNs()) + R"(, "rate": "1/44100", "position": )"
               + std::to_string ((std::int64_t) (position < 0 ? samples : position) - delay) + R"(, "duration": 128, "delay": )"
               + std::to_string (delay) + R"(, "diff": 1.000006, "xrun": )" + std::to_string (xrunCount < 0 ? xrun : xrunCount) + " },";
    }
    static std::string driverRecord (int id, const std::string& name)
    {
        return R"({ "type": "driver", "id": )" + std::to_string (id) + R"(, "name": ")" + name
               + R"(", "prev": 1, "signal": 2, "awake": 3, "finish": 4, "status": "finished", "latency": "128/44100", "xrun_count": 0 },)";
    }
};

struct FakePhc final : PhcDevice
{
    World& world;
    double frequency = 0.0;
    std::optional<double> pendingWrite;       // writeLatency: lands at the next tick
    bool readable = true, writable = true, writeLatency = false;
    std::optional<double> maxAdjust;
    std::vector<double> written;
    explicit FakePhc (World& w) : world (w) {}
    std::optional<PhcSample> read() override
    {
        if (! readable)
            return std::nullopt;
        return PhcSample { world.monoNs(), (std::int64_t) world.phcNs };
    }
    std::optional<double> frequencyPpm() override { return frequency; }
    bool setFrequencyPpm (double ppm) override
    {
        written.push_back (ppm);
        if (! writable)
            return false;
        if (writeLatency)
            pendingWrite = ppm;
        else
            frequency = ppm;
        return true;
    }
    std::optional<double> maxAdjustmentPpm() override { return maxAdjust; }
    std::string problem() const override { return writable ? "" : "Permission denied"; }
};

struct FakeFeed final : ProfilerFeed
{
    std::deque<std::string> lines;
    bool alive = true;
    int starts = 0, refuseStarts = 0;
    bool start() override
    {
        ++starts;
        if (refuseStarts > 0)
        {
            --refuseStarts;
            return false;
        }
        alive = true;
        return true;
    }
    bool drain (const std::function<void (std::string_view)>& line) override
    {
        while (! lines.empty())
        {
            line (lines.front());
            lines.pop_front();
        }
        return alive;
    }
};

struct FakePort final : PtpPortState
{
    std::optional<std::string> state = std::string ("MASTER");
    std::string problem;
    PortReading portState() override { return { state, problem }; }
};

struct Rig
{
    World world;
    FakePhc phc { world };
    FakeFeed feed;
    FakePort port;
    std::vector<ClockStatus> published;
    std::vector<std::string> journal;
    std::vector<double> remembered;
    ClockDaemon daemon;
    bool recording = true;      // the profiler sends the Scarlett's records
    bool otherDriver = false;   // onboard audio drives a graph of its own, interleaved
    int burstTicks = 1;         // the pipe hands records over in bursts of this many ticks
    int tickCount = 0;
    std::vector<std::string> held;

    static DaemonOptions defaults (DaemonOptions options)
    {
        if (options.device.empty())
            options.device = "/dev/ptp0";
        if (options.interface.empty())
            options.interface = "enp5s0";
        return options;
    }

    explicit Rig (DaemonOptions options = {}, std::function<void (Rig&)> before = {})
        : daemon (((before ? before (*this) : void()), phc), feed, &port, defaults (options),
                  [this] (const ClockStatus& s) { published.push_back (s); },
                  [this] (const std::string& line) { journal.push_back (line); },
                  [this] (double f) { remembered.push_back (f); }, world.now) {}

    // `seconds` of the rig at the role's 20 Hz.
    void run (double seconds)
    {
        for (int i = 0; i < (int) std::lround (seconds * 20.0); ++i)
        {
            world.advance (0.05, phc.frequency);
            if (phc.pendingWrite)
                phc.frequency = *std::exchange (phc.pendingWrite, std::nullopt);
            if (recording && feed.alive)
            {
                held.push_back (world.clockRecord());
                held.push_back (World::driverRecord (375, scarlettNode));
            }
            if (otherDriver && feed.alive)
            {
                held.push_back (world.clockRecord (400, 7'777'777.0L, 3));
                held.push_back (World::driverRecord (400, "alsa_output.pci-0000_00_1f.3.analog-stereo"));
            }
            if (++tickCount % burstTicks == 0)
            {
                feed.lines.insert (feed.lines.end(), held.begin(), held.end());
                held.clear();
            }
            daemon.tick (world.now, 1'761'800'000.0 + world.now);
        }
    }

    double secondsUntilLocked (double limit)
    {
        for (double t = 0; t < limit; t += 1.0)
        {
            run (1.0);
            if (daemon.status().health == Health::locked)
                return t + 1.0;
        }
        return -1.0;
    }

    int linesContaining (const std::string& text) const
    {
        int n = 0;
        for (const auto& line : journal)
            n += line.find (text) != std::string::npos;
        return n;
    }

    double worstErrorWhileRunning (double seconds)
    {
        double worst = 0.0;
        for (double t = 0; t < seconds; t += 1.0)
        {
            run (1.0);
            worst = std::max (worst, std::abs (daemon.status().errorNs));
        }
        return worst;
    }
};
}

TEST (ClockDaemon, StartsTheProfilerAndLocksThePhcToTheScarlett)
{
    Rig rig;
    EXPECT_EQ (rig.feed.starts, 1);
    const double seconds = rig.secondsUntilLocked (30.0);
    ASSERT_GT (seconds, 0.0);
    rig.run (600.0);
    const auto& s = rig.daemon.status();
    EXPECT_EQ (s.health, Health::locked);
    EXPECT_NEAR (s.frequencyPpm, 14.1, 0.05);
    EXPECT_LT (std::abs (s.errorNs), 20'000.0);
    ASSERT_TRUE (s.ratePpm);
    EXPECT_NEAR (*s.ratePpm, 0.0, 0.1);
    EXPECT_GE (s.lockedSeconds, 590.0);
    EXPECT_EQ (s.device, "/dev/ptp0");
    EXPECT_EQ (s.interface, "enp5s0");
    EXPECT_EQ (s.portState, "MASTER");
}

TEST (ClockDaemon, PublishesOnceAnUpdateAndLogsOnceEveryTenSeconds)
{
    Rig rig;
    rig.run (60.0);
    EXPECT_NEAR ((double) rig.published.size(), 60.0, 1.0);
    EXPECT_NEAR ((double) rig.linesContaining ("a3-core clock: ") - rig.linesContaining (" -> ") - rig.linesContaining ("port on")
                     - rig.linesContaining ("steering /dev"),
                 6.0, 1.0);
    EXPECT_DOUBLE_EQ (rig.published.back().writtenAtUnix, 1'761'800'000.0 + rig.world.now);
}

// 2026-10-30, the maintainer: the Scarlett's clock is there at once, the PTP clock has to follow at
// once. Cold: about 5 s of rate, one write to it, the phase anchored, locked.
TEST (ClockDaemon, LocksWithinFifteenSecondsWithOneWriteToTheRate)
{
    Rig rig;
    const double seconds = rig.secondsUntilLocked (30.0);
    ASSERT_GT (seconds, 0.0);
    EXPECT_LE (seconds, 15.0);
    ASSERT_FALSE (rig.phc.written.empty());
    EXPECT_NEAR (rig.phc.written.front(), 14.1, 1.5);   // the single jump, straight to the measured rate
    for (std::size_t i = 1; i < rig.phc.written.size(); ++i)
        EXPECT_LE (std::abs (rig.phc.written[i] - rig.phc.written[i - 1]), 0.1 + 1e-9);
}

TEST (ClockDaemon, AWarmStartLocksWithinTenSeconds)
{
    DaemonOptions options;
    options.warmFrequencyPpm = 14.1;
    Rig rig (options);
    const double seconds = rig.secondsUntilLocked (30.0);
    ASSERT_GT (seconds, 0.0);
    EXPECT_LE (seconds, 10.0);
    ASSERT_FALSE (rig.phc.written.empty());
    EXPECT_DOUBLE_EQ (rig.phc.written.front(), 14.1);
}

TEST (ClockDaemon, NeverSlewsFasterThanTheLimitAfterTheJump)
{
    Rig rig;
    rig.run (1200.0);
    ASSERT_GT (rig.phc.written.size(), 10u);
    double worst = 0.0;
    for (std::size_t i = 1; i < rig.phc.written.size(); ++i)
        worst = std::max (worst, std::abs (rig.phc.written[i] - rig.phc.written[i - 1]));
    EXPECT_LE (worst, 0.1 + 1e-9);
}

TEST (ClockDaemon, LocksThroughBurstsJitterAndALateWrite)
{
    Rig rig;
    rig.burstTicks = 6;                   // 300 ms of records at once, as a block-buffered pipe hands them over
    rig.world.delayJitterSamples = 0.55;  // 12.5 us rms per record
    rig.phc.writeLatency = true;
    ASSERT_GT (rig.secondsUntilLocked (400.0), 0.0);
    EXPECT_LT (rig.worstErrorWhileRunning (900.0), 50'000.0);
    EXPECT_EQ (rig.daemon.status().health, Health::locked);
}

TEST (ClockDaemon, AnotherAlsaDriverInterleavedChangesNothing)
{
    Rig rig;
    rig.otherDriver = true;
    ASSERT_GT (rig.secondsUntilLocked (30.0), 0.0);
    rig.run (300.0);
    EXPECT_EQ (rig.daemon.status().health, Health::locked);
    EXPECT_EQ (rig.daemon.status().scarlettBreaks, 0);
    EXPECT_NEAR (rig.daemon.status().frequencyPpm, 14.1, 0.05);
}

TEST (ClockDaemon, OnlyAnotherAlsaDriverIsNoMeasurementAndSaysWhatItSees)
{
    Rig rig;
    rig.recording = false;
    rig.otherDriver = true;
    rig.run (20.0);
    EXPECT_EQ (rig.daemon.status().health, Health::holdover);
    EXPECT_TRUE (rig.phc.written.empty());
    EXPECT_GE (rig.linesContaining ("alsa_output.pci-0000_00_1f.3.analog-stereo"), 1);
}

TEST (ClockDaemon, ObservingNeverWritesAndMeasuresTheRate)
{
    DaemonOptions options;
    options.observeOnly = true;
    Rig rig (options);
    rig.run (120.0);
    EXPECT_TRUE (rig.phc.written.empty());
    const auto& s = rig.daemon.status();
    EXPECT_EQ (s.health, Health::observing);
    EXPECT_TRUE (s.observeOnly);
    ASSERT_TRUE (s.ratePpm);
    EXPECT_NEAR (*s.ratePpm, -14.1, 0.1);   // the PHC runs 14.1 ppm slower than the Scarlett
}

TEST (ClockDaemon, AFailedWriteIsAFaultThatSaysWhy)
{
    Rig rig;
    rig.phc.writable = false;
    rig.run (60.0);
    EXPECT_EQ (rig.daemon.status().health, Health::fault);
    EXPECT_EQ (rig.phc.written.size(), 1u);   // one attempt, then nothing
    EXPECT_GE (rig.linesContaining ("Permission denied"), 1);
}

TEST (ClockDaemon, ARefusedPhcIsNeverMeasuredNorWritten)
{
    DaemonOptions options;
    options.refusal = "/dev/ptp0 is not enp5s0's PTP clock (that is /dev/ptp1)";
    Rig rig (options);
    rig.run (30.0);
    EXPECT_TRUE (rig.phc.written.empty());
    EXPECT_EQ (rig.feed.starts, 0);
    EXPECT_EQ (rig.daemon.status().health, Health::fault);
    EXPECT_EQ (rig.daemon.status().reason, options.refusal);
    EXPECT_FALSE (rig.published.empty());
}

TEST (ClockDaemon, APortThatIsNotMasterPausesTheSteering)
{
    Rig rig;
    ASSERT_GT (rig.secondsUntilLocked (30.0), 0.0);
    rig.port.state = std::string ("SLAVE");   // another grandmaster won
    rig.run (40.0);
    const auto writes = rig.phc.written.size();
    rig.run (30.0);
    EXPECT_EQ (rig.phc.written.size(), writes);
    EXPECT_EQ (rig.daemon.status().health, Health::holdover);
    EXPECT_NE (rig.daemon.status().reason.find ("SLAVE"), std::string::npos);
    rig.port.state = std::string ("MASTER");
    rig.run (40.0);
    ASSERT_GT (rig.secondsUntilLocked (120.0), 0.0);
}

TEST (ClockDaemon, APortStateNobodyCanReadIsReportedEmpty)
{
    Rig rig;
    rig.port.state.reset();
    rig.port.problem = "no PTP state from a3-ptp-state: /run/a3/ptp-state.json missing";
    ASSERT_GT (rig.secondsUntilLocked (30.0), 0.0);   // it still steers ...
    EXPECT_EQ (rig.daemon.status().portState, "");      // ... locked, the port role "not read yet", with why
    EXPECT_EQ (rig.daemon.status().health, Health::locked);
    EXPECT_EQ (rig.daemon.status().portProblem, rig.port.problem);
    EXPECT_GE (rig.linesContaining ("not known"), 1);
}

TEST (ClockDaemon, AWarmStartIsUsedWhenThePhcWasReset)
{
    DaemonOptions options;
    options.warmFrequencyPpm = 14.1;
    Rig rig (options);
    rig.run (1.5);
    ASSERT_FALSE (rig.phc.written.empty());   // written at the first update, no rate measurement
    EXPECT_GT (rig.secondsUntilLocked (30.0), 0.0);
}

TEST (ClockDaemon, AWarmStartIsNotUsedOnAPhcThatRuns)
{
    DaemonOptions options;
    options.warmFrequencyPpm = 20.0;
    Rig rig (options, [] (Rig& r) { r.phc.frequency = 14.1; });   // the daemon restarted; the PHC kept its frequency
    rig.run (10.0);
    for (const double f : rig.phc.written)   // at most the PI's small corrections, never the file's value
        EXPECT_NEAR (f, 14.1, 1.0);
    ASSERT_GT (rig.secondsUntilLocked (120.0), 0.0);
    rig.run (300.0);
    EXPECT_NEAR (rig.daemon.status().frequencyPpm, 14.1, 0.05);
}

TEST (ClockDaemon, ALockedFrequencyIsRememberedOnceAMinute)
{
    Rig rig;
    ASSERT_GT (rig.secondsUntilLocked (30.0), 0.0);
    rig.run (125.0);
    ASSERT_EQ (rig.remembered.size(), 2u);   // after a minute of lock, then once a minute
    EXPECT_NEAR (rig.remembered.back(), 14.1, 0.05);
}

TEST (ClockDaemon, TheRangeStaysInsideTheDevicesOwnLimit)
{
    Rig rig ({}, [] (Rig& r) { r.phc.maxAdjust = 30.0; });
    EXPECT_GE (rig.linesContaining ("range +-10"), 1);   // 30 minus the 20 ppm margin
    rig.run (120.0);
    EXPECT_EQ (rig.daemon.status().health, Health::fault);   // 14.1 ppm is beyond 10: refused before any write
    EXPECT_TRUE (rig.phc.written.empty());
}

TEST (ClockDaemon, ALostProfilerIsHoldoverThenStartedAgain)
{
    Rig rig;
    ASSERT_GT (rig.secondsUntilLocked (30.0), 0.0);
    const auto writes = rig.phc.written.size();
    rig.feed.alive = false;
    rig.feed.refuseStarts = 2;   // pw-profiler does not come back at once (PipeWire restarting)
    rig.run (5.5);
    EXPECT_EQ (rig.daemon.status().health, Health::holdover);
    EXPECT_GE (rig.feed.starts, 3);
    EXPECT_GE (rig.daemon.status().feedRestarts, 2);
    EXPECT_LE (rig.phc.written.size(), writes + 1);   // at most the update that was under way
    EXPECT_EQ (rig.linesContaining ("pw-profiler cannot be started"), 1);   // said once, not every 2 s
    rig.run (120.0);
    EXPECT_EQ (rig.daemon.status().health, Health::locked);
}

TEST (ClockDaemon, ASilentProfilerIsStartedAgain)
{
    Rig rig;
    ASSERT_GT (rig.secondsUntilLocked (30.0), 0.0);
    rig.recording = false;   // alive, but no Scarlett record: a hung PipeWire
    rig.run (12.0);
    EXPECT_EQ (rig.feed.starts, 2);
    EXPECT_GE (rig.linesContaining ("starting the profiler again"), 1);
    rig.recording = true;
    rig.run (120.0);
    EXPECT_EQ (rig.daemon.status().health, Health::locked);
}

TEST (ClockDaemon, AnUnreadablePhcIsHoldoverAndSaidOnce)
{
    Rig rig;
    rig.phc.readable = false;
    rig.run (20.0);
    EXPECT_EQ (rig.daemon.status().health, Health::holdover);
    EXPECT_TRUE (rig.phc.written.empty());
}

TEST (ClockDaemon, AScarlettXrunIsCountedAndReanchored)
{
    Rig rig;
    ASSERT_GT (rig.secondsUntilLocked (30.0), 0.0);
    rig.world.xrun += 1;
    rig.world.samples += 4410.0L;   // and 100 ms lost
    rig.run (60.0);
    const auto& s = rig.daemon.status();
    EXPECT_EQ (s.scarlettBreaks, 1);
    EXPECT_EQ (s.counters.reanchors, 1);
    EXPECT_EQ (s.health, Health::locked);
}

TEST (ClockDaemon, StateChangesAreSaidAtOnce)
{
    Rig rig;
    ASSERT_GT (rig.secondsUntilLocked (30.0), 0.0);
    EXPECT_EQ (rig.linesContaining ("acquiring -> locked"), 1);
}
