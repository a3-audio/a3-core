// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Supervisor.h"

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
}

Supervisor::Supervisor (const a3core::Platform& platformToUse, juce::String selfToUse, a3core::Journal sayToUse)
    : platform (platformToUse), self (std::move (selfToUse)), say (std::move (sayToUse))
{
    if (! say)
        say = [] (const std::string&, bool) {};
}

Supervisor::~Supervisor() { stop(); }

void Supervisor::start()
{
    for (const auto role : { Role::core, Role::engine })
    {
        if (startableRoles (platform, { role }).empty())
        {
            say (unitOf (role) + " is enabled: systemd runs " + name (role) + ", not started here", false);
            continue;
        }
        slots.push_back ({ role, nullptr, std::nullopt });
    }
    for (auto& slot : slots)
        launch (slot, 0.0);
}

void Supervisor::tick (double now)
{
    if (stopped)
        return;
    for (auto& slot : slots)
    {
        if (slot.child != nullptr)
        {
            const auto status = slot.child->poll();
            if (! status)
                continue;
            say (name (slot.role) + " ended (" + howItEnded (*status) + "), starting it again in 1 s", true);
            slot.child.reset();
            slot.restartAt = now + restartAfter;
            continue;
        }
        if (slot.restartAt && now >= *slot.restartAt)
            launch (slot, now);
    }
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

void Supervisor::launch (Slot& slot, double now)
{
    slot.child = platform.spawn (roleCommand (self, slot.role), false);
    if (slot.child == nullptr)
    {
        say ("cannot start " + name (slot.role) + ", trying again in 1 s", true);
        slot.restartAt = now + restartAfter;
        return;
    }
    slot.restartAt.reset();
    say ("started " + name (slot.role) + " (pid " + std::to_string (slot.child->pid()) + ")", false);
}

std::string Supervisor::name (Role role) const { return role == Role::core ? "Core" : "the engine"; }
}
