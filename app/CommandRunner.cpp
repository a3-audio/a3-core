// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CommandRunner.h"
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace a3app
{
namespace
{
std::vector<char*> argvOf (const Command& command)
{
    std::vector<char*> argv;
    for (const auto& word : command)
        argv.push_back (const_cast<char*> (word.c_str()));
    argv.push_back (nullptr);
    return argv;
}

// stdout to `stdoutFd` (or /dev/null with -1), stderr to /dev/null, stdin from /dev/null.
pid_t spawn (const Command& command, int stdoutFd, int closeInChild)
{
    if (command.empty())
        return -1;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init (&actions);
    posix_spawn_file_actions_addopen (&actions, 0, "/dev/null", O_RDONLY, 0);
    if (stdoutFd >= 0)
        posix_spawn_file_actions_adddup2 (&actions, stdoutFd, 1);
    else
        posix_spawn_file_actions_addopen (&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen (&actions, 2, "/dev/null", O_WRONLY, 0);
    if (closeInChild >= 0)
        posix_spawn_file_actions_addclose (&actions, closeInChild);
    auto argv = argvOf (command);
    pid_t pid = -1;
    const int failed = posix_spawnp (&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy (&actions);
    return failed == 0 ? pid : -1;
}

int exitCodeOf (int status) { return WIFEXITED (status) ? WEXITSTATUS (status) : -1; }
}

SpawnRunner::~SpawnRunner()
{
    if (startedPid > 0)
        waitpid (startedPid, nullptr, WNOHANG);   // a restart still running is left to finish; no zombie if it is done
}

CommandRunner::Result SpawnRunner::run (const Command& command)
{
    int pipeFds[2];
    if (pipe2 (pipeFds, O_CLOEXEC) != 0)
        return {};
    const pid_t pid = spawn (command, pipeFds[1], pipeFds[0]);
    close (pipeFds[1]);
    if (pid < 0)
    {
        close (pipeFds[0]);
        return {};
    }
    Result result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (runTimeoutMillis);
    bool timedOut = false;
    for (char buffer[4096];;)
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds> (deadline - std::chrono::steady_clock::now()).count();
        pollfd readable { pipeFds[0], POLLIN, 0 };
        if (left <= 0 || poll (&readable, 1, (int) left) <= 0)
        {
            timedOut = true;
            break;
        }
        const auto n = read (pipeFds[0], buffer, sizeof (buffer));
        if (n <= 0)
            break;   // end of its output
        result.output.append (buffer, (size_t) n);
    }
    close (pipeFds[0]);
    if (timedOut)
        kill (pid, SIGKILL);
    int status = 0;
    waitpid (pid, &status, 0);
    result.exitCode = timedOut ? -1 : exitCodeOf (status);
    return result;
}

bool SpawnRunner::start (const Command& command)
{
    if (startedPid > 0)
        return false;   // one at a time
    startedPid = spawn (command, -1, -1);
    return startedPid > 0;
}

std::optional<int> SpawnRunner::finished()
{
    if (startedPid <= 0)
        return std::nullopt;
    int status = 0;
    if (waitpid (startedPid, &status, WNOHANG) != startedPid)
        return std::nullopt;
    startedPid = -1;
    return exitCodeOf (status);
}
}
