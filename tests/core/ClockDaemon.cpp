// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The clock role's loop against a simulated rig: a Scarlett that the profiler reports every 50 ms and a
// PHC that runs at its own rate plus what was written. Nothing here touches /dev/ptp0.
#include <gtest/gtest.h>

#include <cmath>
#include <deque>
#include <string>
#include <vector>

#include "ClockDaemon.h"

using namespace a3clock;

namespace
{
// The rig as clocks against CLOCK_MONOTONIC: the Scarlett +5.5 ppm, the I210 free-running -8.6 ppm, so
// the Scarlett runs 14.1 ppm faster than the PHC (measured 2026-10-30).
struct World
{
    double now = 100.0;                       // MONOTONIC seconds
    double scarlettPpm = 5.5, phcNaturalPpm = -8.6;
    long double samples = 1'000'000.0L, phcNs = 1.7e18L;   // long double: a PHC in ns needs more than 53 bits
    std::int64_t xrun = 257;
    double sinceRecord = 0.0;

    void advance (double seconds, double phcFrequencyPpm)
    {
        now += seconds;
        samples += 44100.0L * (1.0L + scarlettPpm * 1e-6L) * seconds;
        phcNs += 1e9L * (1.0L + (phcNaturalPpm + phcFrequencyPpm) * 1e-6L) * seconds;
    }
    std::int64_t monoNs() const { return (std::int64_t) std::llround (now * 1e9); }
    std::string record() const
    {
        return R"({ "type": "clock", "flags": 0, "id": 375, "name": "api.alsa.1", "nsec": )" + std::to_string (monoNs())
               + R"(, "rate": "1/44100", "position": )" + std::to_string ((std::int64_t) samples)
               + R"(, "duration": 128, "delay": 0, "diff": 1.000006, "xrun": )" + std::to_string (xrun) + " },";
    }
};

struct FakePhc final : PhcDevice
{
    World& world;
    double frequency = 0.0;
    bool readable = true, writable = true;
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
        frequency = ppm;
        return true;
    }
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

struct Rig
{
    World world;
    FakePhc phc { world };
    FakeFeed feed;
    std::vector<ClockStatus> published;
    std::vector<std::string> journal;
    ClockDaemon daemon;
    bool recording = true;

    explicit Rig (DaemonOptions options = {})
        : daemon (phc, feed, options, [this] (const ClockStatus& s) { published.push_back (s); },
                  [this] (const std::string& line) { journal.push_back (line); }, world.now) {}

    // `seconds` of the rig at the role's 20 Hz.
    void run (double seconds)
    {
        for (int i = 0; i < (int) std::lround (seconds * 20.0); ++i)
        {
            world.advance (0.05, phc.frequency);
            if (recording && feed.alive)
                feed.lines.push_back (world.record());
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
};
}

TEST (ClockDaemon, StartsTheProfilerAndLocksThePhcToTheScarlett)
{
    Rig rig;
    EXPECT_EQ (rig.feed.starts, 1);
    const double seconds = rig.secondsUntilLocked (900.0);
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
}

TEST (ClockDaemon, PublishesOnceAnUpdateAndLogsOnceEveryTenSeconds)
{
    Rig rig;
    rig.run (60.0);
    EXPECT_NEAR ((double) rig.published.size(), 60.0, 1.0);
    EXPECT_NEAR ((double) rig.linesContaining ("a3-core clock: "), 6.0 + rig.linesContaining (" -> "), 1.0);
    EXPECT_DOUBLE_EQ (rig.published.back().writtenAtUnix, 1'761'800'000.0 + rig.world.now);
}

TEST (ClockDaemon, NeverSlewsFasterThanTheLimit)
{
    Rig rig;
    rig.run (1200.0);
    ASSERT_GT (rig.phc.written.size(), 10u);
    double previous = 0.0, worst = 0.0;
    for (const double f : rig.phc.written)
    {
        worst = std::max (worst, std::abs (f - previous));
        previous = f;
    }
    EXPECT_LE (worst, 0.1 + 1e-9);
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
    rig.run (30.0);
    EXPECT_EQ (rig.daemon.status().health, Health::fault);
    EXPECT_EQ (rig.phc.written.size(), 1u);   // one attempt, then nothing
    EXPECT_GE (rig.linesContaining ("Permission denied"), 1);
}

TEST (ClockDaemon, ALostProfilerIsHoldoverThenStartedAgain)
{
    Rig rig;
    ASSERT_GT (rig.secondsUntilLocked (900.0), 0.0);
    const auto writes = rig.phc.written.size();
    rig.feed.alive = false;
    rig.feed.refuseStarts = 2;   // pw-profiler does not come back at once (PipeWire restarting)
    rig.run (5.5);
    EXPECT_EQ (rig.daemon.status().health, Health::holdover);
    EXPECT_GE (rig.feed.starts, 3);
    EXPECT_GE (rig.daemon.status().feedRestarts, 2);
    EXPECT_LE (rig.phc.written.size(), writes + 1);   // at most the update that was under way
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
    ASSERT_GT (rig.secondsUntilLocked (900.0), 0.0);
    rig.world.xrun += 1;
    rig.world.samples += 4410.0;   // and 100 ms lost
    rig.run (60.0);
    const auto& s = rig.daemon.status();
    EXPECT_EQ (s.scarlettBreaks, 1);
    EXPECT_EQ (s.counters.reanchors, 1);
    EXPECT_EQ (s.health, Health::locked);
}

TEST (ClockDaemon, StateChangesAreSaidAtOnce)
{
    Rig rig;
    ASSERT_GT (rig.secondsUntilLocked (900.0), 0.0);
    EXPECT_EQ (rig.linesContaining ("acquiring -> locked"), 1);
}
