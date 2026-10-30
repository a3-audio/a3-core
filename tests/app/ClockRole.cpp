// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The clock role's Linux side, as far as a test may go: the profiler feed with a stand-in command, and a
// PTP clock that is not there. Nothing here opens /dev/ptp0.
#include <gtest/gtest.h>

#include <thread>

#include "ClockRole.h"
#include "Platform.h"

namespace
{
std::vector<std::string> drainUntilEnded (a3app::ProfilerProcess& feed)
{
    std::vector<std::string> lines;
    for (int i = 0; i < 200; ++i)
    {
        const bool alive = feed.drain ([&] (std::string_view line) { lines.emplace_back (line); });
        if (! alive)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return lines;
}
}

TEST (ClockRole, TheFeedHandsOverWholeLinesAndSaysWhenItEnded)
{
    const auto platform = a3core::makePlatform();
    a3app::ProfilerProcess feed (*platform, { "sh", "-c", "printf 'one\\ntwo\\nthr'; sleep 0.1; printf 'ee\\n'" });
    ASSERT_TRUE (feed.start());
    EXPECT_EQ (drainUntilEnded (feed), (std::vector<std::string> { "one", "two", "three" }));
}

TEST (ClockRole, TheFeedStartsAgainAfterItEnded)
{
    const auto platform = a3core::makePlatform();
    a3app::ProfilerProcess feed (*platform, { "sh", "-c", "echo again" });
    ASSERT_TRUE (feed.start());
    EXPECT_EQ (drainUntilEnded (feed).size(), 1u);
    ASSERT_TRUE (feed.start());
    EXPECT_EQ (drainUntilEnded (feed).size(), 1u);
}

TEST (ClockRole, TheProfilerIsPwProfilerAsJson)
{
    EXPECT_EQ (a3app::profilerCommand(), (std::vector<std::string> { "pw-profiler", "-J" }));
}

TEST (ClockRole, AMissingPtpClockIsSaidNotACrash)
{
    a3app::LinuxPhc phc ("/nonexistent/ptp9");
    EXPECT_FALSE (phc.read());
    EXPECT_FALSE (phc.frequencyPpm());
    EXPECT_FALSE (phc.setFrequencyPpm (1.0));
    EXPECT_NE (phc.problem().find ("/nonexistent/ptp9"), std::string::npos);
}

TEST (ClockRole, ScaledPpmIsTheKernelsUnit)
{
    EXPECT_EQ (a3app::scaledPpm (14.1), 924058);   // 14.1 * 65536, rounded
    EXPECT_DOUBLE_EQ (a3app::ppmOfScaled (-65536), -1.0);
}
