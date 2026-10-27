// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "SessionRestart.h"

namespace
{
struct Script
{
    std::vector<juce::String> log;
    int newFailures = 0, previousFailures = 0;   // how many opens fail before one works (-1: always)
    int newTries = 0, previousTries = 0, pauses = 0;

    a3app::RestartSteps steps()
    {
        a3app::RestartSteps s;
        s.quietOld = [this] { log.push_back ("quiet"); };
        s.dropOld = [this] { log.push_back ("dropOld"); };
        s.openNew = [this] { log.push_back ("openNew"); return attempt (newTries, newFailures, "device gone"); };
        s.dropNew = [this] { log.push_back ("dropNew"); };
        s.openPrevious = [this] { log.push_back ("openPrevious"); return attempt (previousTries, previousFailures, "name in use"); };
        s.pauseMs = [this] (int) { ++pauses; };
        return s;
    }
    static juce::Result attempt (int& tries, int failures, const char* why)
    {
        ++tries;
        return failures < 0 || tries <= failures ? juce::Result::fail (why) : juce::Result::ok();
    }
};
}

TEST (SessionRestart, TheOldEngineIsQuietBeforeItIsDropped)
{
    Script script;
    const auto result = a3app::runRestart (script.steps());
    EXPECT_EQ (result.outcome, a3app::RestartOutcome::applied);
    EXPECT_EQ (script.log, (std::vector<juce::String> { "quiet", "dropOld", "openNew" }));
}

TEST (SessionRestart, ATransientClashIsRetriedInABoundedWay)
{
    Script script;
    script.newFailures = 2;
    EXPECT_EQ (a3app::runRestart (script.steps()).outcome, a3app::RestartOutcome::applied);
    EXPECT_EQ (script.newTries, 3);
    EXPECT_EQ (script.pauses, 2);
}

TEST (SessionRestart, ANewConfigThatDoesNotOpenFallsBackToThePreviousOne)
{
    Script script;
    script.newFailures = -1;
    const auto result = a3app::runRestart (script.steps());
    EXPECT_EQ (result.outcome, a3app::RestartOutcome::previousRunning);
    EXPECT_EQ (script.newTries, 3);   // bounded
    EXPECT_EQ (script.previousTries, 1);
    EXPECT_NE (result.message.indexOf ("device gone"), -1);
}

TEST (SessionRestart, WhenNothingOpensNothingIsLeftHalfOpen)
{
    Script script;
    script.newFailures = script.previousFailures = -1;
    const auto result = a3app::runRestart (script.steps());
    EXPECT_EQ (result.outcome, a3app::RestartOutcome::nothingRunning);
    EXPECT_EQ (script.previousTries, 3);   // bounded
    EXPECT_NE (result.message.indexOf ("device gone"), -1);
    EXPECT_NE (result.message.indexOf ("name in use"), -1);
    ASSERT_FALSE (script.log.empty());
    EXPECT_EQ (script.log.back(), "dropNew");   // the last failed open is dropped, not kept
    int opens = 0, drops = 0;
    for (const auto& entry : script.log) { opens += entry.startsWith ("open"); drops += entry == "dropNew"; }
    EXPECT_EQ (opens, drops);   // every failed open was dropped
}
