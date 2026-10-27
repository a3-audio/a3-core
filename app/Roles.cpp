// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Roles.h"

#include <pthread.h>

#include <csignal>

namespace a3app
{
namespace
{
volatile std::sig_atomic_t stopFlag = 0;

void onStopSignal (int) { stopFlag = 1; }
}

void installStopSignals()
{
    struct sigaction action {};
    action.sa_handler = onStopSignal;
    sigemptyset (&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction (SIGTERM, &action, nullptr);
    sigaction (SIGINT, &action, nullptr);
}

bool stopRequested() { return stopFlag != 0; }
void requestStop() { stopFlag = 1; }

ScopedStopSignalsBlocked::ScopedStopSignalsBlocked()
{
    sigset_t stops;
    sigemptyset (&stops);
    sigaddset (&stops, SIGTERM);
    sigaddset (&stops, SIGINT);
    pthread_sigmask (SIG_BLOCK, &stops, &previous);
}

ScopedStopSignalsBlocked::~ScopedStopSignalsBlocked() { pthread_sigmask (SIG_SETMASK, &previous, nullptr); }
void clearStopRequest() { stopFlag = 0; }

std::string unitOf (Role role)
{
    switch (role)
    {
        case Role::core: return "a3-core.service";
        case Role::engine: return "a3-engine.service";
        case Role::window:
        case Role::screencastFanout:
        case Role::i3Bar:
        case Role::supervisor:
        case Role::renderTruth:
        case Role::truthQuery: break;
    }
    return {};
}

std::vector<Role> startableRoles (const a3core::Platform& platform, const std::vector<Role>& wanted)
{
    std::vector<Role> startable;
    for (const auto role : wanted)
    {
        const auto unit = unitOf (role);
        if (unit.empty() || ! platform.unitEnabled (unit))
            startable.push_back (role);
    }
    return startable;
}

std::vector<std::string> roleCommand (const juce::String& self, Role role)
{
    return { self.toStdString(), "--role", roleWord (role) };
}

CoreRole::CoreRole (Stop stopToUse, Finished finishedToUse) : stop (std::move (stopToUse)), finished (std::move (finishedToUse)) {}

std::optional<int> CoreRole::tick (bool stopRequested)
{
    if (stopRequested && ! stopping)
    {
        stopping = true;
        stop();   // state, evening and the address list to disk, every thread joined
    }
    const auto reason = finished (0.0);
    if (! reason)
        return std::nullopt;
    return reason->empty() ? 0 : 1;   // the reason was said by the service itself
}
}
