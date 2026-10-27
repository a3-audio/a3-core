// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of the module cases of a3-core's tools/tests/test_core_send_failures.py (B0 f3345dc7feeb): a
// send that fails is said once and does not end the message. The source-text case
// (EveryOutgoingClientUsesIt) belongs to the task that ports WatchedClient.
#include <gtest/gtest.h>

#include <stdexcept>

#include "SendFailures.h"

using namespace a3core;

namespace
{
constexpr int enetUnreachable = 101;

void unreachable() { throw OsError (enetUnreachable, "Network is unreachable"); }

struct Recorder
{
    Journal journal() { return [this] (const std::string& line, bool toStderr) { said.push_back (line); stderrFlags.push_back (toStderr); }; }

    std::vector<std::string> said;
    std::vector<bool> stderrFlags;
};
}

TEST (SendFailures, ItDoesNotRaise)
{
    Recorder say;
    SendFailures failures (say.journal());
    EXPECT_FALSE (failures.send ("mixer", unreachable));
}

TEST (SendFailures, ItIsSaidOnce)
{
    Recorder say;
    SendFailures failures (say.journal());
    for (int i = 0; i < 5; ++i)
        failures.send ("mixer", unreachable);
    ASSERT_EQ (say.said.size(), 1u);
    EXPECT_NE (say.said[0].find ("mixer"), std::string::npos);
    EXPECT_NE (say.said[0].find ("Network is unreachable"), std::string::npos);
    EXPECT_EQ (say.said[0], "mixer: send failed (Network is unreachable); said once until it gets through again");
    EXPECT_FALSE (say.stderrFlags[0]);   // Python's print: stdout
}

TEST (SendFailures, EachDestinationIsSaidOnItsOwn)
{
    Recorder say;
    SendFailures failures (say.journal());
    failures.send ("mixer", unreachable);
    failures.send ("motion", unreachable);
    EXPECT_EQ (say.said.size(), 2u);
}

TEST (SendFailures, ASendThatGetsThroughIsSaidAndRearms)
{
    Recorder say;
    SendFailures failures (say.journal());
    failures.send ("mixer", unreachable);
    EXPECT_TRUE (failures.send ("mixer", [] {}));
    failures.send ("mixer", unreachable);
    ASSERT_EQ (say.said.size(), 3u);
    EXPECT_NE (say.said[1].find ("again"), std::string::npos);
    EXPECT_EQ (say.said[1], "mixer: sends get through again");
}

TEST (SendFailures, AGoodSendSaysNothing)
{
    Recorder say;
    SendFailures failures (say.journal());
    EXPECT_TRUE (failures.send ("mixer", [] {}));
    EXPECT_TRUE (say.said.empty());
}

TEST (SendFailures, OnlyOsErrorsAreCaught)
{
    Recorder say;
    SendFailures failures (say.journal());
    EXPECT_THROW (failures.send ("mixer", [] { throw std::logic_error ("a bug, not the network"); }), std::logic_error);
}

// `problem.strerror or problem`: an OSError made from a message alone has no strerror, and
// the line carries str(problem) instead.
TEST (SendFailures, AnErrorWithoutAStrerrorIsSaidByItsText)
{
    Recorder say;
    SendFailures failures (say.journal());
    failures.send ("mixer", [] { throw OsError ("network unreachable"); });
    ASSERT_EQ (say.said.size(), 1u);
    EXPECT_EQ (say.said[0], "mixer: send failed (network unreachable); said once until it gets through again");
}
