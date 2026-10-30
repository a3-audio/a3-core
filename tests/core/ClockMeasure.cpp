// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The clock role's measurements: the profiler's clock record, the Scarlett's centroid per window and its
// discontinuities, the PHC between two readings, and the rate of the phase. The recorded data is 400
// records of `pw-profiler -J` on the rig (a3nuc1, 2026-10-30, the Scarlett's driver, id 375).
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "ClockMeasure.h"

using namespace a3clock;

namespace
{
const std::string recordedLine = R"({ "type": "clock", "flags": 0, "id": 375, "name": "api.alsa.1", "nsec": 6954511486491, "rate": "1/44100", "position": 305776640, "duration": 128, "delay": 136, "diff": 1.000006, "next_nsec": 6954514704532, "transport": "stopped", "cycle": 2388880, "xrun": 257 },)";

std::vector<std::string> recorded()
{
    std::ifstream in (std::string (A3_CORE_TEST_DATA) + "/clock/profiler-scarlett.jsonl");
    std::vector<std::string> lines;
    for (std::string line; std::getline (in, line);)
        lines.push_back (line);
    return lines;
}

// A record `seconds` after the first at 44.1 kHz, its position where the clock puts it.
ProfilerClock record (double seconds, std::int64_t xrun = 0, int id = 375, std::string_view name = "api.alsa.1")
{
    ProfilerClock r;
    r.id = id;
    r.name = name;
    r.rate = 44100;
    r.nsec = 1'000'000'000'000LL + (std::int64_t) std::llround (seconds * 1e9);
    r.position = 100'000 + (std::int64_t) std::llround (seconds * 44100.0);
    r.delay = 140;
    r.xrun = xrun;
    return r;
}

const std::string scarlettNode = "alsa_input.usb-Focusrite_Scarlett_18i20_USB_P9P0A77190B57F-00.pro-input-0";

// An intake that knows the Scarlett's driver as node 375, as the profiler's driver record says.
ScarlettIntake scarlettIntake()
{
    ScarlettIntake intake;
    intake.addDriver ({ 375, scarlettNode });
    return intake;
}

void feed (ScarlettIntake& intake, double from, int count, double every = 0.05)
{
    for (int i = 0; i < count; ++i)
        intake.add (record (from + i * every));
}
}

TEST (ClockMeasure, ReadsTheProfilersClockRecord)
{
    const auto r = parseProfilerClock (recordedLine);
    ASSERT_TRUE (r);
    EXPECT_EQ (r->id, 375);
    EXPECT_EQ (r->flags, 0u);
    EXPECT_EQ (r->nsec, 6954511486491LL);
    EXPECT_EQ (r->position, 305776640LL);
    EXPECT_EQ (r->delay, 136);
    EXPECT_EQ (r->rate, 44100);
    EXPECT_EQ (r->xrun, 257);
    EXPECT_EQ (r->name, "api.alsa.1");
}

TEST (ClockMeasure, OtherRecordsAndBrokenLinesAreNotClocks)
{
    EXPECT_FALSE (parseProfilerClock (R"({ "type": "follower", "id": 80, "name": "a3-engine" },)"));
    EXPECT_FALSE (parseProfilerClock (recordedLine.substr (0, 90)));
    EXPECT_FALSE (parseProfilerClock (""));
    EXPECT_FALSE (parseProfilerClock (R"({ "type": "clock", "flags": 0, "id": 375, "name": "x", "nsec": 1, "rate": "1/0", "position": 1, "delay": 0, "xrun": 0 })"));
}

TEST (ClockMeasure, TheRecordedScarlettIsALineWithMicrosecondsOfJitter)
{
    auto intake = scarlettIntake();
    std::vector<ScarlettPoint> points;
    int fed = 0;
    for (const auto& line : recorded())
    {
        const auto r = parseProfilerClock (line);
        ASSERT_TRUE (r) << line;
        EXPECT_EQ (intake.add (*r), fed == 0 ? ScarlettIntake::Taken::discontinuity : ScarlettIntake::Taken::used);
        if (++fed % 20 == 0)   // a second of records
            if (const auto point = intake.take())
                points.push_back (*point);
    }
    ASSERT_EQ (points.size(), 20u);
    EXPECT_EQ (intake.discontinuities(), 0);   // the first record starts the first epoch; no break
    // The line through the centroids: the Scarlett against MONOTONIC (PipeWire said diff 1.000005-6).
    double st = 0, ss = 0, stt = 0, sts = 0;
    const double n = (double) points.size();
    const auto t0 = points.front().monoNs;
    for (const auto& p : points)
    {
        const double t = (double) (p.monoNs - t0), s = (double) p.scarlettNs;
        st += t; ss += s; stt += t * t; sts += t * s;
    }
    const double slope = (n * sts - st * ss) / (n * stt - st * st), intercept = (ss - slope * st) / n;
    EXPECT_NEAR ((slope - 1.0) * 1e6, 5.5, 1.5);
    double squares = 0;
    for (const auto& p : points)
    {
        const double residual = (double) p.scarlettNs - (intercept + slope * (double) (p.monoNs - t0));
        squares += residual * residual;
    }
    EXPECT_LT (std::sqrt (squares / n), 5'000.0);   // ns rms per centroid
}

TEST (ClockMeasure, ACentroidNeedsEnoughRecordsAndKeepsTheWindowUntilThen)
{
    auto intake = scarlettIntake();
    feed (intake, 0.0, 3);
    EXPECT_FALSE (intake.take());
    feed (intake, 0.15, 3);
    const auto point = intake.take();
    ASSERT_TRUE (point);
    EXPECT_EQ (point->records, 6);
    EXPECT_EQ (point->monoNs, record (0.125).nsec);
    EXPECT_NEAR ((double) point->scarlettNs, 0.125e9, 30.0);   // one sample is 22.7 us; rounding only
}

TEST (ClockMeasure, AnXrunStartsANewEpoch)
{
    auto intake = scarlettIntake();
    feed (intake, 0.0, 10);
    const auto before = intake.epoch();
    EXPECT_EQ (intake.add (record (0.5, 1)), ScarlettIntake::Taken::discontinuity);
    EXPECT_EQ (intake.epoch(), before + 1);
    EXPECT_FALSE (intake.take());   // the old window went with the old epoch
}

TEST (ClockMeasure, APositionThatJumpedStartsANewEpoch)
{
    auto intake = scarlettIntake();
    feed (intake, 0.0, 10);
    auto jumped = record (0.5);
    jumped.position += 441;   // 10 ms
    EXPECT_EQ (intake.add (jumped), ScarlettIntake::Taken::discontinuity);
    auto jittered = record (0.55);
    jittered.position += 441 + 1;   // one sample of jitter is not a jump
    EXPECT_EQ (intake.add (jittered), ScarlettIntake::Taken::used);
}

TEST (ClockMeasure, AGapStartsANewEpoch)
{
    auto intake = scarlettIntake();
    feed (intake, 0.0, 10);
    EXPECT_EQ (intake.add (record (2.0)), ScarlettIntake::Taken::discontinuity);
}

TEST (ClockMeasure, FreewheelIsIgnoredAndBreaksTheEpoch)
{
    auto intake = scarlettIntake();
    feed (intake, 0.0, 10);
    auto freewheel = record (0.5);
    freewheel.flags = 1;
    EXPECT_EQ (intake.add (freewheel), ScarlettIntake::Taken::ignored);
    EXPECT_EQ (intake.add (record (0.55)), ScarlettIntake::Taken::discontinuity);
}

TEST (ClockMeasure, ReadsTheProfilersDriverRecord)
{
    const auto d = parseProfilerDriver (R"({ "type": "driver", "id": 375, "name": ")" + scarlettNode
                                        + R"(", "prev": 1, "signal": 2, "awake": 3, "finish": 4, "status": "finished", "latency": "128/44100", "xrun_count": 6 },)");
    ASSERT_TRUE (d);
    EXPECT_EQ (d->id, 375);
    EXPECT_EQ (d->name, scarlettNode);
    EXPECT_FALSE (parseProfilerDriver (recordedLine));
}

TEST (ClockMeasure, NothingCountsBeforeTheScarlettsDriverIsSeen)
{
    ScarlettIntake intake;
    EXPECT_EQ (intake.add (record (0.0)), ScarlettIntake::Taken::ignored);
    intake.addDriver ({ 375, scarlettNode });
    EXPECT_EQ (intake.add (record (0.05)), ScarlettIntake::Taken::discontinuity);   // the first epoch
}

TEST (ClockMeasure, AnotherAlsaDriverInterleavedIsIgnoredWithoutABreak)
{
    auto intake = scarlettIntake();
    intake.addDriver ({ 400, "alsa_output.pci-0000_00_1f.3.analog-stereo" });   // onboard audio drives a graph of its own
    for (int i = 0; i < 20; ++i)
    {
        intake.add (record (i * 0.05));
        auto other = record (i * 0.05 + 0.01, 3, 400, "api.alsa.0");
        other.position += 99'999;   // another crystal, another count
        EXPECT_EQ (intake.add (other), ScarlettIntake::Taken::ignored);
    }
    EXPECT_EQ (intake.discontinuities(), 0);
    const auto point = intake.take();
    ASSERT_TRUE (point);
    EXPECT_EQ (point->records, 20);
}

TEST (ClockMeasure, TheScarlettsPlaybackNodeIsNotItsCapture)
{
    ScarlettIntake intake;
    intake.addDriver ({ 376, "alsa_output.usb-Focusrite_Scarlett_18i20_USB_P9P0A77190B57F-00.pro-output-0" });
    EXPECT_EQ (intake.add (record (0.0, 0, 376)), ScarlettIntake::Taken::ignored);
}

TEST (ClockMeasure, TheScarlettComingBackAsANewNodeIsANewEpoch)
{
    auto intake = scarlettIntake();
    feed (intake, 0.0, 10);
    EXPECT_EQ (intake.lockedId(), 375);
    intake.addDriver ({ 412, scarlettNode });   // replugged
    EXPECT_EQ (intake.add (record (0.5, 0, 375)), ScarlettIntake::Taken::ignored);
    EXPECT_EQ (intake.add (record (0.55, 0, 412)), ScarlettIntake::Taken::discontinuity);
    EXPECT_EQ (intake.lockedId(), 412);
}

TEST (ClockMeasure, BurstyDeliveryWithJitterStillGivesACentroidOnTheLine)
{
    auto intake = scarlettIntake();
    std::mt19937 random (3);
    std::normal_distribution<double> jitter (0.0, 0.55);   // samples: 12.5 us rms
    for (int i = 0; i < 20; ++i)   // a second of records, as one pipe burst
    {
        auto r = record (i * 0.05);
        r.delay = 140 + (std::int64_t) std::lround (jitter (random));
        intake.add (r);
    }
    const auto point = intake.take();
    ASSERT_TRUE (point);
    const double expected = (double) (point->monoNs - record (0.0).nsec);   // the line: Scarlett ns = mono ns
    EXPECT_NEAR ((double) point->scarlettNs, expected, 8'000.0);
}

TEST (ClockMeasure, ALostFeedStartsANewEpoch)
{
    auto intake = scarlettIntake();
    feed (intake, 0.0, 10);
    intake.restart();
    EXPECT_EQ (intake.add (record (0.5)), ScarlettIntake::Taken::discontinuity);
}

TEST (ClockMeasure, ThePhcIsInterpolatedBetweenTwoReadings)
{
    PhcHistory history;
    EXPECT_FALSE (history.phcAt (0));
    history.add ({ 1'000'000'000, 5'000'000'000'000'000'000LL });
    EXPECT_FALSE (history.phcAt (1'000'000'000));
    history.add ({ 2'000'000'000, 5'000'000'001'000'014'100LL });   // +14.1 ppm against MONOTONIC
    EXPECT_EQ (*history.phcAt (1'500'000'000), 5'000'000'000'500'007'050LL);
    EXPECT_EQ (*history.phcAt (2'500'000'000), 5'000'000'001'500'021'150LL);   // extrapolated
    history.add ({ 3'000'000'000, 5'000'000'002'000'028'200LL });
    EXPECT_EQ (*history.phcAt (1'250'000'000), 5'000'000'000'250'003'525LL);   // the older pair
}

TEST (ClockMeasure, TheRateIsTheSlopeOfThePhase)
{
    RateEstimate rate;
    EXPECT_FALSE (rate.ppm());
    for (int i = 0; i < 40; ++i)
        rate.add (1, i, 1000.0 * 14.1 * i + (i % 2 ? 2000.0 : -2000.0));
    ASSERT_TRUE (rate.ppm());
    EXPECT_NEAR (*rate.ppm(), 14.1, 0.05);
    rate.add (2, 41.0, 0.0);
    EXPECT_FALSE (rate.ppm());   // a new epoch starts it again
}
