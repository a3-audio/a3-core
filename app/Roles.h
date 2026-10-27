// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What every role of the program shares: how it is driven from the message thread, how a stop
// signal reaches it, which roles systemd owns, and the core role itself.
#pragma once

#include <csignal>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "Net.h"
#include "Platform.h"
#include "ProgramArgs.h"

namespace a3app
{
// A running role, driven by the app's timer on the message thread. `stopRequested` is true once
// SIGTERM or SIGINT came; an exit code ends the program.
class RoleRunner
{
public:
    virtual ~RoleRunner() = default;
    virtual std::optional<int> tick (bool stopRequested) = 0;
};

// SIGTERM and SIGINT only set a flag (nothing else is safe in a handler); the timer reads it.
void installStopSignals();
bool stopRequested();
void requestStop();        // the same flag, from the message thread (a desktop's quit)
void clearStopRequest();   // the tests'

// While it lives, SIGTERM and SIGINT are blocked on this thread, and so on every thread started
// meanwhile: a role's threads inherit the mask, and the signal reaches the message thread only.
class ScopedStopSignalsBlocked
{
public:
    ScopedStopSignalsBlocked();
    ~ScopedStopSignalsBlocked();
    ScopedStopSignalsBlocked (const ScopedStopSignalsBlocked&) = delete;
    ScopedStopSignalsBlocked& operator= (const ScopedStopSignalsBlocked&) = delete;

private:
    sigset_t previous;
};

// The user unit that runs a role where the package is installed.
std::string unitOf (Role);
// The roles of `wanted` that may be started here: never one whose unit is enabled, since systemd
// starts that one and a second copy would fight it for its ports (Review Focus 4).
std::vector<Role> startableRoles (const a3core::Platform&, const std::vector<Role>& wanted);
// The command line that runs `role` from the program at `self`.
std::vector<std::string> roleCommand (const juce::String& self, Role);

// The core role around a started CoreService: a stop signal calls its stop() here, on the message
// thread, never in the handler; a serve loop that ended by itself (a reason) is exit 1, so that
// systemd starts Core again; one ended by stop() is exit 0.
class CoreRole final : public RoleRunner
{
public:
    using Stop = std::function<void()>;
    using Finished = std::function<std::optional<std::string> (double seconds)>;   // CoreService::waitUntilFinished
    CoreRole (Stop, Finished);
    std::optional<int> tick (bool stopRequested) override;

private:
    Stop stop;
    Finished finished;
    bool stopping = false;
};
}
