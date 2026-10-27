// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The platform seam on Linux: the only place the library and the app read the environment, ask for
// the host name, run systemctl or start, signal and reap a process.
#include "Platform.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "SeenFile.h"

extern char** environ;

namespace a3core
{
namespace
{
using Steady = std::chrono::steady_clock;

class LinuxChild final : public Child
{
public:
    LinuxChild (pid_t childPid, int readEnd) : childPid (childPid), readEnd (readEnd) {}
    ~LinuxChild() override
    {
        if (readEnd >= 0)
            ::close (readEnd);
    }

    int pid() const override { return childPid; }
    int stdoutFd() const override { return readEnd; }

    std::optional<int> poll() override
    {
        if (status)
            return status;
        int raw = 0;
        const auto result = ::waitpid (childPid, &raw, WNOHANG);
        if (result == childPid)
            status = raw;
        else if (result < 0 && errno != EINTR)
            status = 0;   // ECHILD: somebody else reaped it, it is gone
        return status;
    }

    std::optional<int> wait (double seconds) override
    {
        if (seconds < 0)
            return waitForever();
        const auto end = Steady::now() + std::chrono::duration<double> (seconds);
        while (! poll())
        {
            if (Steady::now() >= end)
                return std::nullopt;
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        return status;
    }

    void terminate() override { signal (SIGTERM); }
    void kill() override { signal (SIGKILL); }

private:
    void signal (int number)
    {
        if (! status)   // a reaped pid may already belong to somebody else
            ::kill (childPid, number);
    }

    std::optional<int> waitForever()
    {
        while (! status)
        {
            int raw = 0;
            const auto result = ::waitpid (childPid, &raw, 0);
            if (result == childPid)
                status = raw;
            else if (result < 0 && errno != EINTR)
                status = 0;
        }
        return status;
    }

    pid_t childPid;
    int readEnd;
    std::optional<int> status;
};

// Python's Popen restores signals: an empty mask, and SIGPIPE and SIGTERM at their defaults.
// With ProcessGroup::own the child leads a group of its own (setpgid (0, 0)).
bool setCleanSignals (posix_spawnattr_t& attributes, ProcessGroup group)
{
    sigset_t none, defaults;
    sigemptyset (&none);
    sigemptyset (&defaults);
    sigaddset (&defaults, SIGPIPE);
    sigaddset (&defaults, SIGTERM);
    return posix_spawnattr_setsigmask (&attributes, &none) == 0
           && posix_spawnattr_setsigdefault (&attributes, &defaults) == 0
           && (group == ProcessGroup::inherit || posix_spawnattr_setpgroup (&attributes, 0) == 0)   // 0: a group led by the child
           && posix_spawnattr_setflags (&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF
                                                         | (group == ProcessGroup::own ? POSIX_SPAWN_SETPGROUP : 0)) == 0;
}

std::unique_ptr<Child> spawnProcess (const std::vector<std::string>& command, bool pipeStdout, ProcessGroup group = ProcessGroup::inherit,
                                     ErrorOutput errors = ErrorOutput::inherit)
{
    if (command.empty())
        return nullptr;
    int fds[2] = { -1, -1 };
    if (pipeStdout && ::pipe2 (fds, O_CLOEXEC) != 0)
        return nullptr;

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init (&actions);
    posix_spawnattr_init (&attributes);

    // Python's Popen closes every other descriptor (close_fds); a stray socket in a child would keep
    // a viewer's connection, or one of Core's ports, open after we closed it. So a failure refuses
    // the spawn.
    const bool prepared = (! pipeStdout || posix_spawn_file_actions_adddup2 (&actions, fds[1], 1) == 0)
                          && (errors == ErrorOutput::inherit || posix_spawn_file_actions_addopen (&actions, 2, "/dev/null", O_WRONLY, 0) == 0)
                          && posix_spawn_file_actions_addclosefrom_np (&actions, 3) == 0
                          && setCleanSignals (attributes, group);

    std::vector<std::string> words (command);
    std::vector<char*> argv;
    for (auto& word : words)
        argv.push_back (word.data());
    argv.push_back (nullptr);

    pid_t child = 0;
    int error = EINVAL;
    if (prepared)
        error = ::posix_spawnp (&child, argv[0], &actions, &attributes, argv.data(), environ);
    else
        std::fprintf (stderr, "a3-core: cannot prepare the descriptors of %s, not starting it\n", command[0].c_str());
    posix_spawn_file_actions_destroy (&actions);
    posix_spawnattr_destroy (&attributes);
    if (pipeStdout)
        ::close (fds[1]);
    if (error != 0)
    {
        if (pipeStdout)
            ::close (fds[0]);
        return nullptr;
    }
    if (pipeStdout)
        ::fcntl (fds[0], F_SETFL, ::fcntl (fds[0], F_GETFL) | O_NONBLOCK);
    return std::make_unique<LinuxChild> (child, pipeStdout ? fds[0] : -1);
}

class LinuxPlatform final : public Platform
{
public:
    LinuxPlatform (Environment env, std::string systemctlProgram, double unitQuerySeconds)
        : env (std::move (env)), systemctl (std::move (systemctlProgram)), unitQuerySeconds (unitQuerySeconds) {}

    std::optional<std::string> environment (const std::string& name) const override { return env (name); }

    // Python's Path.home(): $HOME, else the password database.
    juce::File home() const override
    {
        const auto given = env ("HOME");
        if (given && ! given->empty())
            return juce::File (juce::String::fromUTF8 (given->c_str()));
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    }

    juce::File stateDir() const override { return stateDirectory (env ("XDG_STATE_HOME"), home()).getChildFile ("a3-core"); }
    juce::File configDir() const override { return home().getChildFile (".config/a3"); }
    juce::File packageShare() const override { return home().getChildFile (".local/share/a3-core"); }

    std::string hostname() const override
    {
        char name[256] = {};
        if (::gethostname (name, sizeof name - 1) != 0)
            return {};
        return name;
    }

    juce::File self() const override { return juce::File ("/proc/self/exe").getLinkedTarget(); }

    // Without systemctl there is no systemd to own a unit: not enabled. One that does not answer in
    // time may well own it: enabled (nothing is started beside it), said once.
    bool unitEnabled (const std::string& unit) const override
    {
        auto query = spawnProcess ({ systemctl, "--user", "is-enabled", unit }, true);
        if (query == nullptr)
            return false;
        const auto end = Steady::now() + std::chrono::duration<double> (unitQuerySeconds);
        std::optional<int> status;
        while (! (status = query->poll()))
        {
            drain (query->stdoutFd());   // a chatty answer must not block it on a full pipe
            if (Steady::now() >= end)
            {
                query->kill();
                query->wait (-1.0);
                std::fprintf (stderr, "a3-core: systemctl --user is-enabled %s gave no answer within %g s: counted as enabled\n",
                              unit.c_str(), unitQuerySeconds);
                return true;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        return WIFEXITED (*status) && WEXITSTATUS (*status) == 0;
    }

    std::unique_ptr<Child> spawn (const std::vector<std::string>& argv, bool pipeStdout, ProcessGroup group, ErrorOutput errors) const override
    {
        return spawnProcess (argv, pipeStdout, group, errors);
    }

private:
    static void drain (int fd)
    {
        char buffer[256];
        while (::read (fd, buffer, sizeof buffer) > 0)
        {
        }
    }

    Environment env;
    std::string systemctl;
    double unitQuerySeconds;
};

std::optional<std::string> processEnvironment (const std::string& name)
{
    const char* value = std::getenv (name.c_str());
    if (value == nullptr)
        return std::nullopt;
    return std::string (value);
}
}

std::unique_ptr<Platform> makePlatform() { return makePlatform (processEnvironment, "systemctl"); }

std::unique_ptr<Platform> makePlatform (Environment env, std::string systemctl, double unitQuerySeconds)
{
    return std::make_unique<LinuxPlatform> (std::move (env), std::move (systemctl), unitQuerySeconds);
}

Journal streamJournal (std::FILE* out, std::FILE* err)
{
    auto lock = std::make_shared<std::mutex>();
    return [out, err, lock] (const std::string& line, bool toStderr) {
        const auto whole = line + "\n";
        std::FILE* stream = toStderr ? err : out;
        std::lock_guard<std::mutex> hold (*lock);
        std::fwrite (whole.data(), 1, whole.size(), stream);
        std::fflush (stream);
    };
}
}
