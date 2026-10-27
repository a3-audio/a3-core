// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The platform seam on Linux: locations from the environment as B1 read them, the unit question,
// child processes, and the journal the program writes to.
#include <gtest/gtest.h>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <map>
#include <set>
#include <thread>

#include "Platform.h"
#include "SeenFile.h"

using namespace a3core;

namespace
{
Environment environmentOf (std::map<std::string, std::string> values)
{
    return [values] (const std::string& name) -> std::optional<std::string> {
        const auto found = values.find (name);
        if (found == values.end())
            return std::nullopt;
        return found->second;
    };
}

struct Scratch
{
    Scratch() : dir (juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a3-platform", "", false))
    {
        dir.createDirectory();
    }
    ~Scratch() { dir.deleteRecursively(); }
    juce::File dir;
};

std::string readAll (int fd)
{
    std::string text;
    char buffer[4096];
    for (int idle = 0; idle < 400;)
    {
        const auto n = ::read (fd, buffer, sizeof buffer);
        if (n > 0)
        {
            text.append (buffer, static_cast<size_t> (n));
            idle = 0;
        }
        else if (n == 0)
            break;
        else
        {
            ++idle;
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
    }
    return text;
}
}

TEST (Platform, StateDirFollowsXdg)
{
    const auto withXdg = makePlatform (environmentOf ({ { "HOME", "/home/someone" }, { "XDG_STATE_HOME", "/var/xdg" } }), "systemctl");
    EXPECT_EQ (withXdg->stateDir(), juce::File ("/var/xdg/a3-core"));

    const auto without = makePlatform (environmentOf ({ { "HOME", "/home/someone" } }), "systemctl");
    EXPECT_EQ (without->stateDir(), juce::File ("/home/someone/.local/state/a3-core"));
}

TEST (Platform, TheOtherPlacesAreUnderHome)
{
    const auto platform = makePlatform (environmentOf ({ { "HOME", "/home/someone" } }), "systemctl");
    EXPECT_EQ (platform->home(), juce::File ("/home/someone"));
    EXPECT_EQ (platform->configDir(), juce::File ("/home/someone/.config/a3"));
    EXPECT_EQ (platform->packageShare(), juce::File ("/home/someone/.local/share/a3-core"));
}

TEST (Platform, EnvironmentTellsUnsetFromEmpty)
{
    const auto platform = makePlatform (environmentOf ({ { "EMPTY", "" } }), "systemctl");
    EXPECT_EQ (platform->environment ("EMPTY"), std::optional<std::string> (""));
    EXPECT_EQ (platform->environment ("UNSET"), std::nullopt);
}

TEST (Platform, TheProcessEnvironmentIsRead)
{
    ::setenv ("A3_PLATFORM_TEST", "seen", 1);
    EXPECT_EQ (makePlatform()->environment ("A3_PLATFORM_TEST"), std::optional<std::string> ("seen"));
    ::unsetenv ("A3_PLATFORM_TEST");
    EXPECT_EQ (makePlatform()->environment ("A3_PLATFORM_TEST"), std::nullopt);
}

TEST (Platform, HostnameIsTheKernels)
{
    char name[256] = {};
    ::gethostname (name, sizeof name - 1);
    EXPECT_EQ (makePlatform()->hostname(), std::string (name));
}

// The seen file's own lookup goes through the seam too: no second getenv in the library.
TEST (Platform, TheSeenFileAsksTheSamePlace)
{
    EXPECT_EQ (stateDirectory().getChildFile ("a3-core"), makePlatform()->stateDir());
}

TEST (Platform, UnitEnabledAsksSystemctlForTheUserUnit)
{
    Scratch scratch;
    const auto asked = scratch.dir.getChildFile ("asked");
    const auto fake = scratch.dir.getChildFile ("systemctl");
    fake.replaceWithText ("#!/bin/sh\necho \"$@\" >> '" + asked.getFullPathName() + "'\n"
                          "[ \"$3\" = a3-core.service ] && { echo enabled; exit 0; }\necho disabled; exit 1\n",
                          false, false, "\n");   // JUCE's default line feed is \r\n: no shebang survives that
    fake.setExecutePermission (true);
    const auto platform = makePlatform (environmentOf ({}), fake.getFullPathName().toStdString());

    EXPECT_TRUE (platform->unitEnabled ("a3-core.service"));
    EXPECT_FALSE (platform->unitEnabled ("a3-engine.service"));
    EXPECT_EQ (asked.loadFileAsString(), "--user is-enabled a3-core.service\n--user is-enabled a3-engine.service\n");
}

// A systemctl that does not answer in time: the unit counts as managed (the conservative answer:
// nothing is started beside a unit that may well run).
TEST (Platform, ASystemctlThatHangsCountsAsEnabled)
{
    Scratch scratch;
    const auto fake = scratch.dir.getChildFile ("systemctl");
    fake.replaceWithText ("#!/bin/sh\nexec sleep 30\n", false, false, "\n");
    fake.setExecutePermission (true);
    const auto platform = makePlatform (environmentOf ({}), fake.getFullPathName().toStdString(), 0.3);
    const auto began = std::chrono::steady_clock::now();
    EXPECT_TRUE (platform->unitEnabled ("a3-core.service"));
    EXPECT_LT (std::chrono::duration<double> (std::chrono::steady_clock::now() - began).count(), 3.0);
}

TEST (Platform, SelfIsThisExecutable)
{
    EXPECT_EQ (makePlatform()->self(), juce::File ("/proc/self/exe").getLinkedTarget());
}

TEST (Platform, AMissingSystemctlEnablesNothing)
{
    const auto platform = makePlatform (environmentOf ({}), "/nonexistent/systemctl");
    EXPECT_FALSE (platform->unitEnabled ("a3-core.service"));
}

TEST (Platform, ASpawnedChildIsReadAndReaped)
{
    auto child = makePlatform()->spawn ({ "sh", "-c", "echo hello; exit 3" }, true);
    ASSERT_NE (child, nullptr);
    EXPECT_GT (child->pid(), 0);
    ASSERT_GE (child->stdoutFd(), 0);
    EXPECT_EQ (readAll (child->stdoutFd()), "hello\n");
    const auto status = child->wait (5.0);
    ASSERT_TRUE (status.has_value());
    EXPECT_TRUE (WIFEXITED (*status));
    EXPECT_EQ (WEXITSTATUS (*status), 3);
}

// A child asked to have its own process group leads it; a plain one stays in ours.
TEST (Platform, AChildCanHaveItsOwnProcessGroup)
{
    auto own = makePlatform()->spawn ({ "sleep", "30" }, false, ProcessGroup::own);
    ASSERT_NE (own, nullptr);
    EXPECT_EQ (::getpgid (own->pid()), own->pid());
    own->terminate();
    EXPECT_TRUE (own->wait (5.0).has_value());

    auto plain = makePlatform()->spawn ({ "sleep", "30" }, false);
    ASSERT_NE (plain, nullptr);
    EXPECT_EQ (::getpgid (plain->pid()), ::getpgrp());
    plain->terminate();
    EXPECT_TRUE (plain->wait (5.0).has_value());
}

TEST (Platform, AChildWithoutAPipeHasNoStdoutToRead)
{
    auto child = makePlatform()->spawn ({ "true" }, false);
    ASSERT_NE (child, nullptr);
    EXPECT_EQ (child->stdoutFd(), -1);
    EXPECT_TRUE (child->wait (5.0).has_value());
}

TEST (Platform, PollDoesNotWaitAndTerminateEnds)
{
    auto child = makePlatform()->spawn ({ "sleep", "30" }, false);
    ASSERT_NE (child, nullptr);
    EXPECT_EQ (child->poll(), std::nullopt);
    child->terminate();
    const auto status = child->wait (5.0);
    ASSERT_TRUE (status.has_value());
    EXPECT_TRUE (WIFSIGNALED (*status));
    EXPECT_EQ (WTERMSIG (*status), SIGTERM);
}

TEST (Platform, KillEndsAChildThatIgnoresTerminate)
{
    auto child = makePlatform()->spawn ({ "sh", "-c", "trap '' TERM; echo ready; while :; do sleep 0.05; done" }, true);
    ASSERT_NE (child, nullptr);
    char first = 0;
    for (int i = 0; i < 400 && ::read (child->stdoutFd(), &first, 1) != 1; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    child->terminate();
    EXPECT_EQ (child->wait (0.3), std::nullopt);
    child->kill();
    const auto status = child->wait (5.0);
    ASSERT_TRUE (status.has_value());
    EXPECT_EQ (WTERMSIG (*status), SIGKILL);
}

TEST (Platform, AMissingProgramIsNoChild)
{
    EXPECT_EQ (makePlatform()->spawn ({ "a3-no-such-program-anywhere" }, true), nullptr);
}

TEST (Platform, AChildInheritsNoOtherDescriptor)
{
    int fds[2];
    ASSERT_EQ (::pipe (fds), 0);   // not close-on-exec: Popen's close_fds must still keep it out
    auto child = makePlatform()->spawn ({ "sh", "-c", "ls /proc/self/fd" }, true);
    ASSERT_NE (child, nullptr);
    const auto listing = juce::StringArray::fromLines (juce::String (readAll (child->stdoutFd())).trim());
    child->wait (5.0);
    ::close (fds[0]);
    ::close (fds[1]);
    // 0, 1, 2 and the descriptor ls opens for the listing itself.
    EXPECT_LE (listing.size(), 4) << listing.joinIntoString (" ");
    EXPECT_FALSE (listing.contains (juce::String (fds[0])) && listing.contains (juce::String (fds[1])));
}

// Serve, announcer and HTTP threads say lines at once; no line may be torn or held back.
TEST (Platform, JournalLinesAreWholeAndFlushed)
{
    std::FILE* out = std::tmpfile();
    std::FILE* err = std::tmpfile();
    ASSERT_NE (out, nullptr);
    ASSERT_NE (err, nullptr);
    const auto journal = streamJournal (out, err);

    constexpr int threads = 8, lines = 400;
    std::vector<std::thread> sayers;
    for (int t = 0; t < threads; ++t)
        sayers.emplace_back ([&journal, t] {
            const std::string body (200, static_cast<char> ('a' + t));
            for (int i = 0; i < lines; ++i)
                journal (std::to_string (t) + ":" + std::to_string (i) + ":" + body, i % 2 == 1);
        });
    for (auto& sayer : sayers)
        sayer.join();

    // Read through the descriptors, never through the FILE: what is still in its buffer is not out.
    const auto readFd = [] (std::FILE* stream) {
        const int fd = ::fileno (stream);
        ::lseek (fd, 0, SEEK_SET);
        return readAll (fd);
    };
    std::set<std::string> seen;
    for (auto* stream : { out, err })
    {
        const auto text = readFd (stream);
        ASSERT_FALSE (text.empty());
        ASSERT_EQ (text.back(), '\n');
        for (const auto& line : juce::StringArray::fromLines (juce::String (text).trimEnd()))
        {
            const auto parts = juce::StringArray::fromTokens (line, ":", "");
            ASSERT_EQ (parts.size(), 3) << line;
            const int t = parts[0].getIntValue();
            EXPECT_EQ (parts[2].toStdString(), std::string (200, static_cast<char> ('a' + t))) << line;
            EXPECT_EQ (parts[1].getIntValue() % 2 == 1, stream == err) << line;
            seen.insert (line.toStdString());
        }
    }
    EXPECT_EQ (seen.size(), static_cast<size_t> (threads * lines));
    std::fclose (out);
    std::fclose (err);
}
