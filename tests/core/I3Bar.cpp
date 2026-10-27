// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// B1's a3-bar-per-workspace.py (a3-core 7a7b2441f74e) had no Python tests of its own beyond
// test_rig_workspaces.BarPerWorkspace (bar_mode); the event loop is pinned here against a fake i3-msg.
#include <fcntl.h>
#include <unistd.h>

#include <map>
#include <memory>

#include <gtest/gtest.h>

#include "I3Bar.h"

using namespace a3core;

TEST (I3Bar, ModeByWorkspace)
{
    const std::map<std::string, std::string> expected {
        { "1", "invisible" }, { "2: StemDeck", "invisible" }, { "3", "dock" }, { "10", "dock" },
        { "", "invisible" }, { "x", "invisible" },
        // test_rig_workspaces.BarPerWorkspace
        { "1:MOTION", "invisible" }, { "2:STEMDECK", "invisible" }, { "2", "invisible" },
        { "3:ENGINE", "dock" }, { "4:QJACKCTL", "dock" }, { "5:SCARLETT", "dock" }, { "7:DESK", "dock" }, { "7", "dock" },
        { "6:STEMDECK B", "invisible" }, { "scratch", "invisible" },
        // B1's STEMDECK rule is by name, wherever the number is, and case-sensitive
        { "9:STEMDECK C", "invisible" }, { "6: stemdeck", "dock" }, { "STEMDECK", "invisible" },
        // the number is read from the start, and a long one is still a number
        { " 3", "invisible" }, { "03:X", "dock" }, { "007", "dock" }, { "0", "invisible" },
        { "99999999999999999999", "dock" }, { "3x4", "dock" } };
    for (const auto& [name, mode] : expected)
        EXPECT_EQ (barMode (name), mode) << "'" << name << "'";
}

TEST (I3Bar, OnlyFocusEventsCount)
{
    EXPECT_EQ (focusChange (R"({"change":"focus","current":{"name":"3:ENGINE"},"old":{"name":"1"}})"), std::optional<std::string> ("dock"));
    EXPECT_EQ (focusChange (R"({"change":"focus","current":{"name":"2:STEMDECK"}})"), std::optional<std::string> ("invisible"));
    EXPECT_EQ (focusChange (R"({"change":"focus"})"), std::optional<std::string> ("invisible"));   // no current: not a tool workspace
    EXPECT_EQ (focusChange (R"({"change":"focus","current":null})"), std::optional<std::string> ("invisible"));
    EXPECT_EQ (focusChange (R"({"change":"focus","current":{"num":4}})"), std::optional<std::string> ("invisible"));

    for (const char* line : { R"({"change":"init","current":{"name":"3"}})", R"({"change":"empty","current":{"name":"3"}})",
                              R"({"change":"urgent","current":{"name":"3"}})", R"({"current":{"name":"3"}})",
                              "not json", "", "[1,2]", "3", "null" })
        EXPECT_FALSE (focusChange (line).has_value()) << line;
}

namespace
{
class FakeChild final : public Child
{
public:
    explicit FakeChild (int fd, int status = 0) : readFd (fd), exitStatus (status) {}
    ~FakeChild() override
    {
        if (readFd >= 0)
            ::close (readFd);
    }
    int pid() const override { return 4242; }
    int stdoutFd() const override { return readFd; }
    std::optional<int> poll() override { return exitStatus; }
    std::optional<int> wait (double) override { return exitStatus; }
    void terminate() override {}
    void kill() override {}

private:
    int readFd;
    int exitStatus;
};

// An i3-msg on paper: the focused workspace and the event lines are given, every `bar mode` call is kept.
class FakeI3 final : public Platform
{
public:
    std::string workspaces = "[]";
    std::string events;
    bool failToStart = false;
    int eventsStatus = 0;
    mutable std::vector<std::string> barCalls;   // "dock" / "invisible"
    mutable std::vector<std::vector<std::string>> spawns;

    std::optional<std::string> environment (const std::string&) const override { return std::nullopt; }
    juce::File home() const override { return {}; }
    juce::File stateDir() const override { return {}; }
    juce::File configDir() const override { return {}; }
    juce::File packageShare() const override { return {}; }
    std::string hostname() const override { return "fake"; }
    juce::File self() const override { return {}; }
    bool unitEnabled (const std::string&) const override { return false; }

    std::unique_ptr<Child> spawn (const std::vector<std::string>& argv, bool pipeStdout, ProcessGroup) const override
    {
        spawns.push_back (argv);
        if (failToStart)
            return nullptr;
        if (argv.size() == 3 && argv[1] == "-q")   // {"i3-msg", "-q", "bar mode <mode> a3"}
        {
            const auto& command = argv[2];
            const std::string prefix = "bar mode ", suffix = " a3";
            EXPECT_EQ (command.rfind (prefix, 0), 0u) << command;
            barCalls.push_back (command.substr (prefix.size(), command.size() - prefix.size() - suffix.size()));
            return std::make_unique<FakeChild> (-1);
        }
        EXPECT_TRUE (pipeStdout);
        const bool isEvents = argv.size() == 5 && argv[2] == "subscribe";
        return piped (isEvents ? events : workspaces, isEvents ? eventsStatus : 0);
    }

private:
    static std::unique_ptr<Child> piped (const std::string& text, int status)
    {
        int fds[2];
        EXPECT_EQ (::pipe2 (fds, O_CLOEXEC | O_NONBLOCK), 0);
        EXPECT_EQ (::write (fds[1], text.data(), text.size()), static_cast<ssize_t> (text.size()));
        ::close (fds[1]);
        return std::make_unique<FakeChild> (fds[0], status);
    }
};

std::string event (const std::string& change, const std::string& workspace)
{
    return R"({"change":")" + change + R"(","current":{"name":")" + workspace + R"("}})" + "\n";
}
}

TEST (I3Bar, TheBarIsSetForTheFocusedWorkspaceFirst)
{
    FakeI3 i3;
    i3.workspaces = R"([{"name":"1:MOTION","focused":false},{"name":"3:ENGINE","focused":true}])";
    EXPECT_EQ (runI3Bar (i3), 0);
    ASSERT_GE (i3.spawns.size(), 3u);
    EXPECT_EQ (i3.spawns[0], (std::vector<std::string> { "i3-msg", "-t", "get_workspaces" }));
    EXPECT_EQ (i3.spawns[1], (std::vector<std::string> { "i3-msg", "-q", "bar mode dock a3" }));
    EXPECT_EQ (i3.spawns[2], (std::vector<std::string> { "i3-msg", "-t", "subscribe", "-m", R"(["workspace"])" }));
}

TEST (I3Bar, NoWorkspaceFocusedOrNothingAnsweredKeepsTheBarAway)
{
    FakeI3 none;
    none.workspaces = R"([{"name":"3","focused":false}])";
    runI3Bar (none);
    EXPECT_EQ (none.barCalls, (std::vector<std::string> { "invisible" }));

    FakeI3 silent;
    silent.workspaces = "";   // `or "[]"`
    runI3Bar (silent);
    EXPECT_EQ (silent.barCalls, (std::vector<std::string> { "invisible" }));
}

TEST (I3Bar, NoRepeatOfTheSameMode)
{
    FakeI3 i3;
    i3.workspaces = R"([{"name":"3:ENGINE","focused":true}])";
    i3.events = event ("focus", "3:ENGINE") + event ("focus", "4:QJACKCTL") + event ("focus", "1:MOTION")
              + "garbage\n" + event ("init", "9") + event ("focus", "2:STEMDECK") + event ("focus", "5:SCARLETT")
              + event ("focus", "6:STEMDECK B");
    runI3Bar (i3);
    // the first call is the start; then only the changes: dock -> invisible -> dock -> invisible
    EXPECT_EQ (i3.barCalls, (std::vector<std::string> { "dock", "invisible", "dock", "invisible" }));
}

TEST (I3Bar, ALastLineWithoutANewlineCounts)
{
    FakeI3 i3;
    i3.workspaces = R"([{"name":"1","focused":true}])";
    i3.events = event ("focus", "3");
    i3.events.pop_back();
    runI3Bar (i3);
    EXPECT_EQ (i3.barCalls, (std::vector<std::string> { "invisible", "dock" }));
}

TEST (I3Bar, TheEventStreamsExitCodeIsTheBarsOwn)
{
    FakeI3 i3;
    i3.eventsStatus = 3 << 8;   // exited with 3
    EXPECT_EQ (runI3Bar (i3), 3);
}

TEST (I3Bar, WithoutI3MsgItFails)
{
    FakeI3 i3;
    i3.failToStart = true;
    EXPECT_EQ (runI3Bar (i3), 1);
}

TEST (I3Bar, AStopEndsItAtOnce)
{
    FakeI3 i3;
    const std::atomic<bool> stop { true };
    EXPECT_EQ (runI3Bar (i3, &stop), 0);
}
