// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "I3Bar.h"

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <functional>

#include "Json.h"

namespace a3core
{
namespace
{
constexpr int firstToolWorkspace = 3;

// re.match(r"(\d+)", name): the number the name starts with; nullopt without one. A number too long
// for an int is still at least the first tool workspace.
std::optional<int> leadingNumber (const std::string& name)
{
    long long number = 0;
    size_t digits = 0;
    for (; digits < name.size() && name[digits] >= '0' && name[digits] <= '9'; ++digits)
        number = std::min<long long> (number * 10 + (name[digits] - '0'), 1000000);
    if (digits == 0)
        return std::nullopt;
    return static_cast<int> (number);
}

std::string nameOf (const juce::var& workspace)
{
    const auto name = workspace.getProperty ("name", juce::var());
    return name.isString() ? name.toString().toStdString() : std::string();
}

// The wait status as the exit code a shell would show.
int exitCodeOf (int status)
{
    if (WIFEXITED (status))
        return WEXITSTATUS (status);
    return WIFSIGNALED (status) ? 128 + WTERMSIG (status) : status;
}

bool stopped (const std::atomic<bool>* stop) { return stop != nullptr && stop->load(); }

// Reads the child's stdout, calling `onLine` for each line (the last one needs no newline), until the
// stream ends or `stop` asks. False if it was stopped.
bool readLines (Child& child, const std::atomic<bool>* stop, const std::function<void (const std::string&)>& onLine)
{
    std::string pending;
    char buffer[4096];
    for (;;)
    {
        if (stopped (stop))
            return false;
        pollfd waiting { child.stdoutFd(), POLLIN, 0 };
        const int ready = ::poll (&waiting, 1, 100);
        if (ready < 0 && errno != EINTR)
            break;
        if (ready <= 0)
            continue;
        const auto count = ::read (waiting.fd, buffer, sizeof buffer);
        if (count < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (count <= 0)
            break;
        pending.append (buffer, static_cast<size_t> (count));
        for (auto newline = pending.find ('\n'); newline != std::string::npos; newline = pending.find ('\n'))
        {
            onLine (pending.substr (0, newline));
            pending.erase (0, newline + 1);
        }
    }
    if (! pending.empty())
        onLine (pending);
    return true;
}

// The focused workspace's name; nullopt if `stop` came first (or i3-msg could not be started: `started`).
// B1's capture_output swallowed i3-msg's complaint that i3 was not up yet, so its stderr goes nowhere.
std::optional<std::string> focusedWorkspace (const Platform& platform, const std::atomic<bool>* stop, bool& started)
{
    auto query = platform.spawn ({ "i3-msg", "-t", "get_workspaces" }, true, ProcessGroup::inherit, ErrorOutput::discard);
    started = query != nullptr;
    if (! started)
        return std::string();
    std::string answer;
    if (! readLines (*query, stop, [&] (const std::string& line) { answer += line + "\n"; }))
    {
        query->terminate();
        query->wait (2.0);
        return std::nullopt;
    }
    query->wait (5.0);

    juce::String error;
    const auto workspaces = parseJson (juce::String::fromUTF8 (answer.c_str()), error);
    if (error.isNotEmpty() || workspaces.getArray() == nullptr)
        return std::string();
    for (const auto& workspace : *workspaces.getArray())
        if (static_cast<bool> (workspace.getProperty ("focused", false)))
            return nameOf (workspace);
    return std::string();
}

// subprocess.run waits for i3-msg; a stop ends the wait (the call is quick and reaps itself).
void setBar (const Platform& platform, const std::string& mode, const std::atomic<bool>* stop)
{
    auto call = platform.spawn ({ "i3-msg", "-q", "bar mode " + mode + " " + i3BarId }, false);
    if (call == nullptr)
        return;
    for (int slices = 0; slices < 50 && ! stopped (stop); ++slices)
        if (call->wait (0.1))
            return;
}
}

std::string barMode (const std::string& workspaceName)
{
    if (workspaceName.find ("STEMDECK") != std::string::npos)   // deck B on 6 fills its screen like deck A on 2
        return "invisible";
    const auto number = leadingNumber (workspaceName);
    return number && *number >= firstToolWorkspace ? "dock" : "invisible";
}

std::optional<std::string> focusChange (const std::string& eventLine)
{
    juce::String error;
    const auto event = parseJson (juce::String::fromUTF8 (eventLine.c_str()), error);
    if (error.isNotEmpty() || event.getDynamicObject() == nullptr)
        return std::nullopt;
    if (event.getProperty ("change", juce::var()).toString() != "focus")
        return std::nullopt;
    return barMode (nameOf (event.getProperty ("current", juce::var())));
}

int runI3Bar (const Platform& platform, const std::atomic<bool>* stop)
{
    bool started = false;
    const auto focused = focusedWorkspace (platform, stop, started);
    if (! started)
    {
        std::fprintf (stderr, "a3-core: i3-msg could not be started\n");
        return 1;
    }
    if (! focused)
        return 0;
    auto shown = barMode (*focused);
    setBar (platform, shown, stop);
    if (stopped (stop))
        return 0;

    auto events = platform.spawn ({ "i3-msg", "-t", "subscribe", "-m", R"(["workspace"])" }, true);
    if (events == nullptr)
    {
        std::fprintf (stderr, "a3-core: i3-msg could not be started\n");
        return 1;
    }
    const bool ended = readLines (*events, stop, [&] (const std::string& line) {
        const auto mode = focusChange (line);
        if (! mode || *mode == shown)
            return;
        setBar (platform, *mode, stop);
        shown = *mode;
    });
    if (! ended)
    {
        events->terminate();
        events->wait (2.0);
        return 0;
    }
    const auto status = events->wait (-1.0);
    return status ? exitCodeOf (*status) : 0;
}
}
