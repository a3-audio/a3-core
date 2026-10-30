// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's posix_spawn runner, with harmless commands (never pw-metadata or systemctl).
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include "CommandRunner.h"

TEST (CommandRunner, RunsWaitsAndCapturesStdout)
{
    a3app::SpawnRunner runner;
    const auto echoed = runner.run ({ "echo", "clock.force-quantum" });
    EXPECT_EQ (echoed.exitCode, 0);
    EXPECT_EQ (echoed.output, "clock.force-quantum\n");
    EXPECT_EQ (runner.run ({ "false" }).exitCode, 1);
    EXPECT_EQ (runner.run ({ "a3-no-such-command" }).exitCode, -1);
    EXPECT_EQ (runner.run ({}).exitCode, -1);
}

TEST (CommandRunner, AStartedCommandIsPolledNotWaitedFor)
{
    a3app::SpawnRunner runner;
    ASSERT_TRUE (runner.start ({ "sh", "-c", "exit 3" }));
    EXPECT_FALSE (runner.start ({ "true" })) << "one at a time";
    std::optional<int> exit;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (! exit && std::chrono::steady_clock::now() < deadline)
    {
        exit = runner.finished();
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    EXPECT_EQ (exit, std::optional<int> (3));
    EXPECT_FALSE (runner.finished().has_value());
    EXPECT_TRUE (runner.start ({ "true" }));
}
