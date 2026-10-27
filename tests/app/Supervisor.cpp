// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The supervisor (--headless), the rule that no role systemd owns is started a second time, the core
// role's stop and exit, and the stop signal.
#include <gtest/gtest.h>

#include <csignal>

#include "FakePlatform.h"
#include "Roles.h"
#include "Supervisor.h"

using a3app::Role;

namespace
{
const juce::String self ("/usr/bin/a3-core");
const std::vector<std::string> coreCommand { "/usr/bin/a3-core", "--role", "core" };
const std::vector<std::string> engineCommand { "/usr/bin/a3-core", "--role", "engine" };
}

TEST (Supervisor, StartsCoreAndEngine)
{
    fake::FakePlatform platform;
    a3app::Supervisor supervisor (platform, self);
    supervisor.start();
    ASSERT_EQ (platform.children.size(), 2u);
    EXPECT_EQ (platform.children[0]->argv, coreCommand);
    EXPECT_EQ (platform.children[1]->argv, engineCommand);

    supervisor.tick (100.0);   // both run: nothing more
    EXPECT_EQ (platform.children.size(), 2u);
}

TEST (Supervisor, RestartsAFailedEngineAfterOneSecond)
{
    fake::FakePlatform platform;
    a3app::Supervisor supervisor (platform, self);
    supervisor.start();
    supervisor.tick (10.0);
    platform.spawned (engineCommand)[0]->status = 1 << 8;   // exit 1

    supervisor.tick (10.1);   // seen as ended here
    EXPECT_EQ (platform.spawned (engineCommand).size(), 1u);
    supervisor.tick (11.0);
    EXPECT_EQ (platform.spawned (engineCommand).size(), 1u);
    supervisor.tick (11.2);
    EXPECT_EQ (platform.spawned (engineCommand).size(), 2u);
    EXPECT_EQ (platform.spawned (coreCommand).size(), 1u);   // Core ran on, untouched
    EXPECT_EQ (platform.spawned (coreCommand)[0]->terminated, 0);
}

TEST (Supervisor, RestartsAFailedCoreAfterOneSecondToo)
{
    fake::FakePlatform platform;
    a3app::Supervisor supervisor (platform, self);
    supervisor.start();
    platform.spawned (coreCommand)[0]->status = 1 << 8;
    supervisor.tick (5.0);
    supervisor.tick (5.9);
    EXPECT_EQ (platform.spawned (coreCommand).size(), 1u);
    supervisor.tick (6.0);
    EXPECT_EQ (platform.spawned (coreCommand).size(), 2u);
}

TEST (Supervisor, StopEndsBoth)
{
    fake::FakePlatform platform;
    a3app::Supervisor supervisor (platform, self);
    supervisor.start();
    supervisor.stop();
    for (const auto& child : platform.children)
    {
        EXPECT_EQ (child->terminated, 1);
        EXPECT_EQ (child->killed, 0);
        EXPECT_TRUE (child->status.has_value());
    }
    supervisor.tick (100.0);   // a stopped supervisor starts nothing again
    EXPECT_EQ (platform.children.size(), 2u);
}

TEST (Supervisor, StopKillsAChildThatIgnoresTerminate)
{
    fake::FakePlatform platform;
    a3app::Supervisor supervisor (platform, self);
    supervisor.start();
    platform.children[1]->endsOnTerminate = false;
    supervisor.stop();
    EXPECT_EQ (platform.children[1]->terminated, 1);
    EXPECT_EQ (platform.children[1]->killed, 1);
    EXPECT_EQ (platform.children[0]->killed, 0);
}

TEST (Supervisor, TheDestructorStopsToo)
{
    fake::FakePlatform platform;
    {
        a3app::Supervisor supervisor (platform, self);
        supervisor.start();
    }
    for (const auto& child : platform.children)
        EXPECT_EQ (child->terminated, 1);
}

// Carried (g), Review Focus 4: a role whose user unit is enabled belongs to systemd; the supervisor
// (and the window, Task 26) never starts a second one.
TEST (Roles, SpawnsNothingAUnitManages)
{
    {
        fake::FakePlatform platform;
        platform.enabledUnits = { "a3-core.service" };
        a3app::Supervisor supervisor (platform, self);
        supervisor.start();
        supervisor.tick (1.0);
        supervisor.tick (50.0);
        EXPECT_EQ (platform.spawned (coreCommand).size(), 0u);
        EXPECT_EQ (platform.spawned (engineCommand).size(), 1u);
        EXPECT_EQ (platform.unitsAsked, (std::vector<std::string> { "a3-core.service", "a3-engine.service" }));
    }
    {
        fake::FakePlatform platform;
        platform.enabledUnits = { "a3-core.service", "a3-engine.service" };
        a3app::Supervisor supervisor (platform, self);
        supervisor.start();
        supervisor.tick (50.0);
        EXPECT_TRUE (platform.children.empty());
    }
    {
        fake::FakePlatform platform;
        platform.enabledUnits = { "a3-engine.service" };
        EXPECT_EQ (a3app::startableRoles (platform, { Role::core, Role::engine }), std::vector<Role> { Role::core });
    }
    EXPECT_EQ (a3app::unitOf (Role::core), "a3-core.service");
    EXPECT_EQ (a3app::unitOf (Role::engine), "a3-engine.service");
    EXPECT_EQ (a3app::roleCommand (self, Role::engine), engineCommand);
}

// Carried (f): the engine role is Plan 4's Session without its window.
TEST (Roles, TheEngineRoleOpensNoWindow)
{
    for (const auto* name : { "EngineRole.h", "EngineRole.cpp" })
    {
        const auto source = juce::File (A3_APP_SOURCE_DIR).getChildFile (name).loadFileAsString();
        ASSERT_TRUE (source.isNotEmpty()) << name;
        EXPECT_FALSE (source.contains ("MainWindow")) << name;
        EXPECT_FALSE (source.contains ("DocumentWindow")) << name;
    }
}

// Carried (b): the stop runs on the message thread from the timer's tick, never in the handler.
TEST (CoreRole, AStopSignalStopsTheServiceFromTheTick)
{
    int stops = 0;
    std::optional<std::string> finished;
    a3app::CoreRole role ([&] { ++stops; finished = ""; }, [&] (double) { return finished; });

    EXPECT_EQ (role.tick (false), std::nullopt);
    EXPECT_EQ (stops, 0);
    EXPECT_EQ (role.tick (true), std::optional<int> (0));
    EXPECT_EQ (stops, 1);
    EXPECT_EQ (role.tick (true), std::optional<int> (0));
    EXPECT_EQ (stops, 1);   // once
}

TEST (CoreRole, ALoopThatEndedByItselfExitsNonZero)
{
    int stops = 0;
    std::optional<std::string> finished;
    a3app::CoreRole role ([&] { ++stops; }, [&] (double) { return finished; });
    EXPECT_EQ (role.tick (false), std::nullopt);
    finished = "the serve loop ended: poll failed";
    EXPECT_EQ (role.tick (false), std::optional<int> (1));
    EXPECT_EQ (stops, 0);
}

TEST (CoreRole, AStopThatHasNotFinishedYetWaits)
{
    std::optional<std::string> finished;
    a3app::CoreRole role ([] {}, [&] (double) { return finished; });
    EXPECT_EQ (role.tick (true), std::nullopt);
    finished = "";
    EXPECT_EQ (role.tick (true), std::optional<int> (0));
}

TEST (StopSignal, TermAndIntOnlySetTheFlag)
{
    a3app::installStopSignals();
    for (const int signal : { SIGTERM, SIGINT })
    {
        a3app::clearStopRequest();
        EXPECT_FALSE (a3app::stopRequested());
        std::raise (signal);
        EXPECT_TRUE (a3app::stopRequested());
    }
    a3app::clearStopRequest();
    std::signal (SIGTERM, SIG_DFL);
    std::signal (SIGINT, SIG_DFL);
}
