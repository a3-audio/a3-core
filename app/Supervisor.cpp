// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Supervisor.h"

#include <signal.h>
#include <sys/wait.h>

#include "Roles.h"

namespace a3app
{
namespace
{
std::string howItEnded (int status)
{
    if (WIFEXITED (status))
        return "exit " + std::to_string (WEXITSTATUS (status));
    if (WIFSIGNALED (status))
        return "signal " + std::to_string (WTERMSIG (status));
    return "status " + std::to_string (status);
}

// systemd's Restart=on-failure: exit 0 and the clean signals (SIGHUP, SIGINT, SIGTERM, SIGPIPE) are
// not failures.
bool failed (int status)
{
    if (WIFEXITED (status))
        return WEXITSTATUS (status) != 0;
    if (WIFSIGNALED (status))
    {
        const int signal = WTERMSIG (status);
        return signal != SIGHUP && signal != SIGINT && signal != SIGTERM && signal != SIGPIPE;
    }
    return true;
}
}

Supervisor::Supervisor (const a3core::Platform& platformToUse, juce::String selfToUse, a3core::Journal sayToUse)
    : platform (platformToUse), self (std::move (selfToUse)), say (std::move (sayToUse))
{
    if (! say)
        say = [] (const std::string&, bool) {};
}

Supervisor::~Supervisor() { stop(); }

void Supervisor::start (double now)
{
    for (const auto role : { Role::core, Role::engine })
    {
        if (startableRoles (platform, { role }).empty())
        {
            say (unitOf (role) + " is enabled: systemd runs " + name (role) + ", not started here", false);
            continue;
        }
        slots.push_back ({ role, nullptr, std::nullopt, {}, false, false });
    }
    for (auto& slot : slots)
        launch (slot, now);
}

std::optional<int> Supervisor::finished() const
{
    bool anyGivenUp = false;
    for (const auto& slot : slots)
    {
        if (! slot.ended)
            return std::nullopt;
        anyGivenUp = anyGivenUp || slot.gaveUp;
    }
    return anyGivenUp ? 1 : 0;
}

void Supervisor::tick (double now)
{
    if (stopped)
        return;
    for (auto& slot : slots)
    {
        if (slot.child != nullptr)
        {
            if (const auto status = slot.child->poll())
                ended (slot, *status, now);
            continue;
        }
        if (slot.restartAt && now >= *slot.restartAt)
            launch (slot, now);
    }
}

void Supervisor::ended (Slot& slot, int status, double now)
{
    slot.child.reset();
    if (! failed (status))
    {
        say (name (slot.role) + " ended (" + howItEnded (status) + "), not started again", false);
        slot.ended = true;
        return;
    }
    while (! slot.starts.empty() && slot.starts.front() < now - startWindow)
        slot.starts.pop_front();
    if (static_cast<int> (slot.starts.size()) >= startLimit)
    {
        say (name (slot.role) + " failed (" + howItEnded (status) + ") after " + std::to_string (startLimit) + " starts within "
                 + std::to_string (static_cast<int> (startWindow)) + " s: given up",
             true);
        slot.ended = slot.gaveUp = true;
        return;
    }
    say (name (slot.role) + " failed (" + howItEnded (status) + "), starting it again in 1 s", true);
    slot.restartAt = now + restartAfter;
}

void Supervisor::stop()
{
    if (stopped)
        return;
    stopped = true;
    for (auto& slot : slots)
        if (slot.child != nullptr && ! slot.child->poll())
            slot.child->terminate();
    for (auto& slot : slots)
    {
        if (slot.child == nullptr)
            continue;
        if (! slot.child->wait (stopGrace))
        {
            say (name (slot.role) + " did not end on SIGTERM within " + std::to_string (static_cast<int> (stopGrace)) + " s, killed", true);
            slot.child->kill();
            slot.child->wait (-1.0);
        }
        slot.child.reset();
    }
}

// A start that cannot even spawn counts as a failed start, as systemd counts it.
void Supervisor::launch (Slot& slot, double now)
{
    slot.restartAt.reset();
    slot.starts.push_back (now);
    slot.child = platform.spawn (roleCommand (self, slot.role), false);
    if (slot.child == nullptr)
    {
        say ("cannot start " + name (slot.role), true);
        ended (slot, W_EXITCODE (1, 0), now);
        return;
    }
    say ("started " + name (slot.role) + " (pid " + std::to_string (slot.child->pid()) + ")", false);
}

std::string Supervisor::name (Role role) const { return role == Role::core ? "Core" : "the engine"; }
}
