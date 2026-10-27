// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// B1's a3-bar-per-workspace.py (a3-core 7a7b2441f74e) had no Python tests of its own beyond
// test_rig_workspaces.BarPerWorkspace (bar_mode); the event loop is pinned here against a fake i3-msg.
#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <map>
#include <memory>
#include <thread>

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
        { "3:ENGINE", "dock" }, { "4", "dock" }, { "5:SCARLETT", "dock" }, { "7:DESK", "dock" }, { "7", "dock" },
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
    explicit FakeChild (int fd, std::optional<int> status = 0, int heldOpen = -1) : readFd (fd), exitStatus (status), holdFd (heldOpen) {}
    ~FakeChild() override
    {
        if (readFd >= 0)
            ::close (readFd);
        if (holdFd >= 0)
            ::close (holdFd);
    }
    int pid() const override { return 4242; }
    int stdoutFd() const override { return readFd; }
    std::optional<int> poll() override { return exitStatus; }
    std::optional<int> wait (double) override { return exitStatus; }   // a nullopt status: never ends
    void terminate() override {}
    void kill() override {}

private:
    int readFd;
    std::optional<int> exitStatus;
    int holdFd;   // the write end, kept open: the stream never ends
};

// An i3-msg on paper: the focused workspace and the event lines are given, every `bar mode` call is kept.
class FakeI3 final : public Platform
{
public:
    std::string workspaces = "[]";
    std::string events;
    bool failToStart = false;
    int eventsStatus = 0;
    bool workspacesNeverEnd = false;             // get_workspaces keeps its pipe open
    bool barCallsNeverEnd = false;               // an i3-msg that does not return
    std::atomic<bool>* stopWhenBarIsSet = nullptr;
    mutable std::vector<std::string> barCalls;   // "dock" / "invisible"
    mutable std::vector<std::vector<std::string>> spawns;
    mutable std::vector<ErrorOutput> errorOutputs;   // per spawn

    std::optional<std::string> environment (const std::string&) const override { return std::nullopt; }
    juce::File home() const override { return {}; }
    juce::File stateDir() const override { return {}; }
    juce::File configDir() const override { return {}; }
    juce::File packageShare() const override { return {}; }
    std::string hostname() const override { return "fake"; }
    juce::File self() const override { return {}; }
    bool unitEnabled (const std::string&) const override { return false; }
    bool unitActive (const std::string&) const override { return false; }

    std::unique_ptr<Child> spawn (const std::vector<std::string>& argv, bool pipeStdout, ProcessGroup, ErrorOutput errors) const override
    {
        spawns.push_back (argv);
        errorOutputs.push_back (errors);
        if (failToStart)
            return nullptr;
        if (argv.size() == 3 && argv[1] == "-q")   // {"i3-msg", "-q", "bar mode <mode> a3"}
        {
            const auto& command = argv[2];
            const std::string prefix = "bar mode ", suffix = " a3";
            EXPECT_EQ (command.rfind (prefix, 0), 0u) << command;
            barCalls.push_back (command.substr (prefix.size(), command.size() - prefix.size() - suffix.size()));
            if (stopWhenBarIsSet != nullptr)
                *stopWhenBarIsSet = true;
            return std::make_unique<FakeChild> (-1, barCallsNeverEnd ? std::nullopt : std::optional<int> (0));
        }
        EXPECT_TRUE (pipeStdout);
        const bool isEvents = argv.size() == 5 && argv[2] == "subscribe";
        return piped (isEvents ? events : workspaces, isEvents ? eventsStatus : 0, ! isEvents && workspacesNeverEnd);
    }

private:
    static std::unique_ptr<Child> piped (const std::string& text, int status, bool keepOpen)
    {
        int fds[2];
        EXPECT_EQ (::pipe2 (fds, O_CLOEXEC | O_NONBLOCK), 0);
        EXPECT_EQ (::write (fds[1], text.data(), text.size()), static_cast<ssize_t> (text.size()));
        if (keepOpen)
            return std::make_unique<FakeChild> (fds[0], status, fds[1]);
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
    i3.events = event ("focus", "3:ENGINE") + event ("focus", "4") + event ("focus", "1:MOTION")
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

// B1's capture_output=True swallowed get_workspaces' stderr; the events and the bar calls kept it.
TEST (I3Bar, OnlyTheWorkspaceQueryDiscardsItsErrors)
{
    FakeI3 i3;
    i3.workspaces = R"([{"name":"3","focused":true}])";
    runI3Bar (i3);
    ASSERT_EQ (i3.spawns.size(), i3.errorOutputs.size());
    ASSERT_GE (i3.spawns.size(), 3u);
    EXPECT_EQ (i3.errorOutputs[0], ErrorOutput::discard);   // get_workspaces
    EXPECT_EQ (i3.errorOutputs[1], ErrorOutput::inherit);   // bar mode
    EXPECT_EQ (i3.errorOutputs[2], ErrorOutput::inherit);   // subscribe
}

TEST (I3Bar, AStopEndsTheFirstQueryToo)
{
    FakeI3 i3;
    i3.workspacesNeverEnd = true;
    std::atomic<bool> stop { false };
    std::thread later ([&] {
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
        stop = true;
    });
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ (runI3Bar (i3, &stop), 0);
    later.join();
    EXPECT_LT (std::chrono::steady_clock::now() - start, std::chrono::seconds (3));
    EXPECT_TRUE (i3.barCalls.empty());
}

TEST (I3Bar, AStopEndsABarCallThatDoesNotReturn)
{
    FakeI3 i3;
    i3.workspaces = R"([{"name":"3","focused":true}])";
    i3.barCallsNeverEnd = true;
    std::atomic<bool> stop { false };
    i3.stopWhenBarIsSet = &stop;   // the stop comes while that call is out
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ (runI3Bar (i3, &stop), 0);
    EXPECT_LT (std::chrono::steady_clock::now() - start, std::chrono::seconds (3));
    for (const auto& argv : i3.spawns)
        EXPECT_NE (argv[2], "subscribe");   // it never went on to follow the events
}
