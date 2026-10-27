// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The platform seam (O7): where files live, the environment, the host name, whether systemd runs a
// unit, and child processes. Everything that asks the operating system goes through here, so that the
// rest of the program -- Core, the roles, the supervisor -- is told rather than looks things up, and
// tests hand in a fake. Linux is the only implementation (PlatformLinux.cpp).
#pragma once

#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "Net.h"

namespace a3core
{
// A process this program started. Owning it does not end it: the owner terminates, kills or waits.
class Child
{
public:
    virtual ~Child() = default;
    virtual int pid() const = 0;
    // The read end of the child's stdout (non-blocking), or -1 when it was not piped.
    virtual int stdoutFd() const = 0;
    // The wait status once the child has ended (reaped here), nullopt while it runs. Never blocks.
    // A child somebody else reaped counts as ended with status 0.
    virtual std::optional<int> poll() = 0;
    // As poll(), waiting up to `seconds` (negative: until it ends).
    virtual std::optional<int> wait (double seconds) = 0;
    virtual void terminate() = 0;   // SIGTERM
    virtual void kill() = 0;        // SIGKILL
};

class Platform
{
public:
    virtual ~Platform() = default;
    // An environment variable; nullopt when unset (set to nothing is "").
    virtual std::optional<std::string> environment (const std::string& name) const = 0;
    virtual juce::File home() const = 0;
    virtual juce::File stateDir() const = 0;       // $XDG_STATE_HOME/a3-core or ~/.local/state/a3-core
    virtual juce::File configDir() const = 0;      // ~/.config/a3
    virtual juce::File packageShare() const = 0;   // ~/.local/share/a3-core
    virtual std::string hostname() const = 0;
    // `systemctl --user is-enabled <unit>` succeeds: systemd starts it, so nobody else may.
    virtual bool unitEnabled (const std::string& unit) const = 0;
    // argv[0] is looked up in PATH. The child starts with Python's Popen defaults: no signal blocked,
    // SIGPIPE and SIGTERM at their defaults, no descriptor but 0, 1 and 2 (1 is the pipe when
    // `pipeStdout`). nullptr when it could not be started.
    virtual std::unique_ptr<Child> spawn (const std::vector<std::string>& argv, bool pipeStdout) const = 0;
};

using Environment = std::function<std::optional<std::string> (const std::string& name)>;

// This machine: the process's environment, `systemctl` from PATH.
std::unique_ptr<Platform> makePlatform();
// The same with the environment and the systemctl program given (the tests).
std::unique_ptr<Platform> makePlatform (Environment, std::string systemctl);

// The program's journal on two streams: each line whole (one write under one lock, whichever thread
// says it) and flushed at once, as B1's `python3 -u` printed.
Journal streamJournal (std::FILE* out, std::FILE* err);
}
