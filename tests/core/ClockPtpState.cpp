// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// ptp4l's port state as the clock role reads it: /run/a3/ptp-state.json, written as root every 10 s by
// a3-ptp-state.timer (ptp4l's sandboxed unit never answers a user's pmc on the Core).
#include <gtest/gtest.h>

#include "ClockPtpState.h"

using namespace a3clock;

namespace
{
struct TempFile
{
    juce::File file = juce::File::createTempFile ("a3-ptp-state");
    ~TempFile() { file.deleteFile(); }
    void write (const juce::String& state, const juce::String& problem, double at)
    {
        file.replaceWithText (R"({ "portState": ")" + state + R"(", "problem": ")" + problem + R"(", "writtenAtUnix": )"
                              + juce::String ((juce::int64) at) + " }\n");
    }
};
constexpr double now = 1'761'830'000.0;
}

TEST (ClockPtpState, AFreshMasterIsMaster)
{
    TempFile t;
    t.write ("MASTER", "", now - 4);
    const auto reading = readPtpState (t.file, now);
    EXPECT_EQ (reading.state, std::optional<std::string> ("MASTER"));
    EXPECT_EQ (reading.problem, "");
}

TEST (ClockPtpState, AnotherStateIsThatState)
{
    TempFile t;
    t.write ("SLAVE", "", now - 4);
    EXPECT_EQ (readPtpState (t.file, now).state, std::optional<std::string> ("SLAVE"));
}

TEST (ClockPtpState, AStaleFileIsNoState)
{
    TempFile t;
    t.write ("MASTER", "", now - ptpStateStaleSeconds - 1);
    const auto reading = readPtpState (t.file, now);
    EXPECT_FALSE (reading.state);
    EXPECT_NE (reading.problem.find ("no PTP state from a3-ptp-state"), std::string::npos) << reading.problem;
    EXPECT_NE (reading.problem.find ("old"), std::string::npos) << reading.problem;
}

TEST (ClockPtpState, AMissingFileIsNoState)
{
    const auto reading = readPtpState (juce::File ("/nonexistent/a3/ptp-state.json"), now);
    EXPECT_FALSE (reading.state);
    EXPECT_NE (reading.problem.find ("no PTP state from a3-ptp-state"), std::string::npos) << reading.problem;
}

TEST (ClockPtpState, NoAnswerFromPtp4lIsNoStateWithItsReason)
{
    TempFile t;
    t.write ("", "no answer from ptp4l", now - 2);
    const auto reading = readPtpState (t.file, now);
    EXPECT_FALSE (reading.state);
    EXPECT_NE (reading.problem.find ("no answer from ptp4l"), std::string::npos) << reading.problem;
}

TEST (ClockPtpState, ABrokenFileIsNoState)
{
    TempFile t;
    t.file.replaceWithText ("{ half");
    EXPECT_FALSE (readPtpState (t.file, now).state);
}

TEST (ClockPtpState, TheFileIsUnderRun)
{
    EXPECT_EQ (ptpStateFile().getFullPathName(), "/run/a3/ptp-state.json");
}
