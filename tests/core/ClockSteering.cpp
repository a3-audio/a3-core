// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The clock loop against synthetic clocks: a PHC that runs at its natural rate plus what was written,
// a Scarlett at 1, measurement noise, dropouts, steps, discontinuities and drift. It has to lock within
// its window and never move the frequency faster than its slew limit or outside its range.
#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <random>
#include <utility>

#include "ClockSteering.h"

using namespace a3clock;

namespace
{
// The PHC minus the Scarlett, in ns, as the clocks run: d(phase)/dt = 1000 ns/s per ppm of
// (natural + written). The measurement adds noise and an arbitrary PTP epoch.
struct Plant
{
    double naturalPpm = -14.1;            // the I210 free-running against the Scarlett (measured: Scarlett +14.1 ppm faster)
    double writtenPpm = 0.0;
    double phaseNs = 0.0;
    double noiseNs = 3'000.0;             // the profiler centroid of a second: about 3 us
    std::int64_t epochOffsetNs = 1'761'800'000'123'456'789LL;   // PTP time is far from the Scarlett's count
    bool ignoresWrites = false;
    std::mt19937 random { 7 };

    void advance (double seconds) { phaseNs += 1000.0 * (naturalPpm + writtenPpm) * seconds; }
    Measurement measure (std::uint64_t epoch = 1)
    {
        std::normal_distribution<double> noise (0.0, noiseNs);
        return { epoch, epochOffsetNs + (std::int64_t) std::llround (phaseNs + noise (random)) };
    }
};

struct Loop
{
    SteeringConfig config;
    Plant plant;
    ClockSteering steering;
    double now = 0.0;
    double maxSlew = 0.0;                 // the largest |frequency change| per second seen
    double minFrequency = 1e9, maxFrequency = -1e9;
    int writes = 0;
    std::uint64_t epoch = 1;
    double worstError = 0.0;              // the largest |error| once anchored: the receivers' FIFO walk
    bool writeLatency = false;            // a write takes effect one update later
    std::optional<double> pendingWrite;

    explicit Loop (SteeringConfig c = {}, double start = 0.0, std::optional<double> warm = std::nullopt)
        : config (c), steering (c, start, 0.0, warm) { plant.writtenPpm = start; }

    // `seconds` updates, one a second; `measured` false: nothing comes in.
    Decision step (bool measured = true)
    {
        now += 1.0;
        plant.advance (1.0);
        if (pendingWrite)
            plant.writtenPpm = *std::exchange (pendingWrite, std::nullopt);
        const double before = steering.frequencyPpm();
        const auto decision = steering.update (now, measured ? std::optional<Measurement> (plant.measure (epoch)) : std::nullopt);
        if (decision.write)
        {
            ++writes;
            if (writeLatency)
                pendingWrite = *decision.write;
            else if (! plant.ignoresWrites)
                plant.writtenPpm = *decision.write;
        }
        if (steering.tracking() && decision.health != Health::fault)
            worstError = std::max (worstError, std::abs (decision.errorNs));
        maxSlew = std::max (maxSlew, std::abs (steering.frequencyPpm() - before));
        minFrequency = std::min (minFrequency, steering.frequencyPpm());
        maxFrequency = std::max (maxFrequency, steering.frequencyPpm());
        return decision;
    }

    void run (int seconds, bool measured = true)
    {
        for (int i = 0; i < seconds; ++i)
            step (measured);
    }

    // Seconds until locked, or -1 within `limit`.
    int untilLocked (int limit)
    {
        for (int i = 1; i <= limit; ++i)
            if (step().health == Health::locked)
                return i;
        return -1;
    }
};

constexpr double slewTolerance = 1e-9;
}

TEST (ClockSteering, LocksFromTheMeasuredFourteenPpmWithinFiveMinutes)
{
    Loop run;
    const int seconds = run.untilLocked (300);
    ASSERT_GT (seconds, 0);
    double worst = 0.0;
    for (int i = 0; i < 3600; ++i)
    {
        const auto decision = run.step();
        ASSERT_EQ (decision.health, Health::locked) << "after " << i << " s";
        worst = std::max (worst, std::abs (decision.errorNs));
    }
    EXPECT_LT (worst, 50'000.0);
    EXPECT_NEAR (run.steering.frequencyPpm(), 14.1, 0.1);
}

// The phase error from the anchor on is every receiver's FIFO walk: frequency first keeps it small.
TEST (ClockSteering, ThePhaseWalkWhileAcquiringStaysSmall)
{
    Loop run;
    ASSERT_GT (run.untilLocked (300), 0);
    EXPECT_LT (run.worstError, 150'000.0);
}

TEST (ClockSteering, NeverSlewsFasterThanTheLimitWhileAcquiring)
{
    Loop run;
    run.run (1800);
    EXPECT_LE (run.maxSlew, 0.1 + slewTolerance);
}

TEST (ClockSteering, NeverLeavesItsRangeAndLocksNearItsEdge)
{
    Loop run;
    run.plant.naturalPpm = -39.0;   // close to the edge: the phase pull pushes against it
    EXPECT_GT (run.untilLocked (900), 0);
    run.run (3600);
    EXPECT_LE (run.maxFrequency, 40.0 + slewTolerance);
    EXPECT_GE (run.minFrequency, -40.0 - slewTolerance);
    EXPECT_EQ (run.steering.health(), Health::locked);
}

TEST (ClockSteering, LocksAtNineteenAndAHalfPpm)
{
    Loop run;
    run.plant.naturalPpm = -19.5;
    EXPECT_GT (run.untilLocked (600), 0);
    EXPECT_LT (run.worstError, 150'000.0);
}

TEST (ClockSteering, AWarmStartSlewsToTheLastLockAndLocksSooner)
{
    Loop run ({}, 0.0, 14.1);   // after a boot the driver reset the PHC to 0; the state file says 14.1
    EXPECT_TRUE (run.step().write);   // no rate measurement: it slews from the first update
    const int seconds = run.untilLocked (300);
    ASSERT_GT (seconds, 0);
    EXPECT_LT (seconds, 141 + 30);   // 14.1 ppm at 0.1 ppm/s, then the lock
    EXPECT_LT (run.worstError, 50'000.0);
    EXPECT_LE (run.maxSlew, 0.1 + slewTolerance);
}

TEST (ClockSteering, AWarmStartOutsideTheRangeIsIgnored)
{
    Loop run ({}, 0.0, 55.0);
    EXPECT_FALSE (run.step().write);   // measures the rate instead
    EXPECT_GT (run.untilLocked (300), 0);
}

TEST (ClockSteering, LocksWithAWriteThatTakesEffectOneUpdateLate)
{
    Loop run;
    run.writeLatency = true;
    ASSERT_GT (run.untilLocked (400), 0);
    double worst = 0.0;
    for (int i = 0; i < 1800; ++i)
        worst = std::max (worst, std::abs (run.step().errorNs));
    EXPECT_LT (worst, 50'000.0);
}

TEST (ClockSteering, LocksWithTheJitterOfSingleProfilerRecords)
{
    Loop run;
    run.plant.noiseNs = 12'500.0;   // one record's jitter, measured on the rig
    ASSERT_GT (run.untilLocked (600), 0);
    double worst = 0.0;
    for (int i = 0; i < 1800; ++i)
        worst = std::max (worst, std::abs (run.step().errorNs));
    EXPECT_LT (worst, 50'000.0);
    EXPECT_LE (run.maxSlew, 0.1 + slewTolerance);
}

TEST (ClockSteering, FollowsATemperatureDriftWhileLocked)
{
    Loop run;
    ASSERT_GT (run.untilLocked (900), 0);
    for (int i = 0; i < 600; ++i)   // one ppm in ten minutes, far more than the Scarlett's 0.33 ppm sd
    {
        run.plant.naturalPpm -= 1.0 / 600.0;
        const auto decision = run.step();
        ASSERT_EQ (decision.health, Health::locked);
        ASSERT_LT (std::abs (decision.errorNs), 50'000.0);
    }
}

TEST (ClockSteering, ARestartOnALockedClockChangesNothing)
{
    Loop run ({}, 14.1);   // the PHC as a dead daemon left it
    run.plant.phaseNs = 0.0;
    EXPECT_GT (run.untilLocked (120), 0);
    run.run (600);
    EXPECT_GT (run.minFrequency, 14.1 - 0.3);
    EXPECT_LT (run.maxFrequency, 14.1 + 0.3);
}

TEST (ClockSteering, NoMeasurementAtStartIsHoldover)
{
    Loop run;
    EXPECT_EQ (run.step (false).health, Health::acquiring);
    run.run (2, false);
    const auto decision = run.step (false);
    EXPECT_EQ (decision.health, Health::holdover);
    EXPECT_FALSE (decision.write);
    EXPECT_EQ (run.writes, 0);
}

TEST (ClockSteering, HoldsTheFrequencyThroughADropoutAndLocksAgain)
{
    Loop run;
    ASSERT_GT (run.untilLocked (900), 0);
    run.run (60);
    const double held = run.steering.frequencyPpm();
    const int writesBefore = run.writes;
    for (int i = 0; i < 20; ++i)
    {
        const auto decision = run.step (false);
        EXPECT_FALSE (decision.write);
        if (i >= 3)
        {
            EXPECT_EQ (decision.health, Health::holdover);
        }
    }
    EXPECT_EQ (run.writes, writesBefore);
    EXPECT_EQ (run.steering.frequencyPpm(), held);
    EXPECT_EQ (run.steering.counters().holdovers, 1);
    EXPECT_NE (run.step().health, Health::holdover);
    EXPECT_GT (run.untilLocked (120), 0);
    EXPECT_LE (run.maxSlew, 0.1 + slewTolerance);
}

TEST (ClockSteering, ReanchorsAScarlettDiscontinuityKeepingTheError)
{
    Loop run;
    ASSERT_GT (run.untilLocked (900), 0);
    run.run (30);
    const double before = run.steering.errorNs();
    run.epoch = 2;                         // an xrun: the Scarlett's count starts again ...
    run.plant.phaseNs += 37'000'000.0;     // ... 37 ms away
    const auto decision = run.step();
    EXPECT_EQ (run.steering.counters().reanchors, 1);
    EXPECT_LT (std::abs (decision.errorNs - before), 20'000.0);
    EXPECT_EQ (decision.health, Health::locked);
    run.run (600);
    EXPECT_EQ (run.steering.health(), Health::locked);
    EXPECT_LE (run.maxSlew, 0.1 + slewTolerance);
}

TEST (ClockSteering, ASteppedPtpClockIsReanchoredNotSlewed)
{
    Loop run;
    ASSERT_GT (run.untilLocked (900), 0);
    run.plant.phaseNs += 5'000'000.0;      // somebody stepped the PHC by 5 ms
    const auto decision = run.step();
    EXPECT_EQ (run.steering.counters().phcSteps, 1);
    EXPECT_LT (std::abs (decision.errorNs), 50'000.0);
    EXPECT_NE (decision.health, Health::fault);
    run.run (600);
    EXPECT_EQ (run.steering.health(), Health::locked);
    EXPECT_LE (run.maxSlew, 0.1 + slewTolerance);
}

TEST (ClockSteering, StopsWhenTheErrorRunsAwayAfterALock)
{
    Loop run;
    ASSERT_GT (run.untilLocked (900), 0);
    run.plant.ignoresWrites = true;        // a loop bug: the clock no longer follows what is written
    run.plant.naturalPpm += 5.0;
    Decision decision;
    for (int i = 0; i < 1000 && decision.health != Health::fault; ++i)
        decision = run.step();
    ASSERT_EQ (decision.health, Health::fault);
    const int writesAtFault = run.writes;
    const double held = run.steering.frequencyPpm();
    run.run (100);
    EXPECT_EQ (run.writes, writesAtFault);
    EXPECT_EQ (run.steering.frequencyPpm(), held);
    EXPECT_EQ (run.steering.health(), Health::fault);
}

TEST (ClockSteering, ACrystalBeyondTheRangeIsAFaultBeforeAnyWrite)
{
    Loop run;
    run.plant.naturalPpm = -45.0;
    Decision decision;
    for (int i = 0; i < 60 && decision.health != Health::fault; ++i)
        decision = run.step();
    EXPECT_EQ (decision.health, Health::fault);
    EXPECT_EQ (run.writes, 0);
}

TEST (ClockSteering, ASteppedPtpClockDuringAHoldoverIsReanchoredNotAFault)
{
    Loop run;
    ASSERT_GT (run.untilLocked (300), 0);
    run.run (60);
    run.run (300, false);                  // five minutes without a PHC reading ...
    run.plant.phaseNs += 3'000'000.0;      // ... during which an igb reset stepped the PHC by 3 ms
    const auto decision = run.step();
    EXPECT_EQ (run.steering.counters().phcSteps, 1);
    EXPECT_NE (decision.health, Health::fault);
    EXPECT_GT (run.untilLocked (120), 0);
}

TEST (ClockSteering, AFailedWriteIsAFaultAndNothingIsWrittenAgain)
{
    Loop run;
    run.run (5);
    run.steering.writeFailed ("no write access");
    EXPECT_EQ (run.steering.health(), Health::fault);
    EXPECT_STREQ (run.steering.reason(), "no write access");
    const int writes = run.writes;
    run.run (60);
    EXPECT_EQ (run.writes, writes);
}

TEST (ClockSteering, EveryHealthHasAWord)
{
    EXPECT_STREQ (healthWord (Health::acquiring), "acquiring");
    EXPECT_STREQ (healthWord (Health::locked), "locked");
    EXPECT_STREQ (healthWord (Health::holdover), "holdover");
    EXPECT_STREQ (healthWord (Health::fault), "fault");
    EXPECT_STREQ (healthWord (Health::observing), "observing");
}
