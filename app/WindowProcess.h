// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window role's process rules (P9, the laptop rule): it probes Core (core.web) and the engine
// (engine.control); a role that does not answer and whose user unit is not enabled is started as its own
// child (on the rig the units run them, so nothing is started there). On quit it asks once whether to stop
// its own children; it never stops a role it did not start and never touches a service.
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "Platform.h"
#include "ProgramArgs.h"
#include "ui/CoreClient.h"
#include "ui/RemoteHostView.h"

namespace a3app
{
class WindowProcess
{
public:
    // How long a child is given to end on SIGTERM before SIGKILL.
    static constexpr double stopWaitSeconds = 5.0;

    WindowProcess (const a3core::Platform&, CoreClient&, RemoteHostView&);
    ~WindowProcess();

    WindowProcess (const WindowProcess&) = delete;
    WindowProcess& operator= (const WindowProcess&) = delete;

    // Probes both; starts what does not answer and may be started here. The roles it started.
    std::vector<Role> attach();
    bool coreAnswers() const;
    bool engineAnswers() const;

    // Its own children that still run, in start order.
    std::vector<Role> children();

    // The question to ask before quitting, once: nullopt when nothing of its own runs or it was asked.
    std::optional<juce::String> quitQuestion();
    // Ends the window's part: with `stopChildren` its own children are stopped (SIGTERM, SIGKILL after
    // stopWaitSeconds), otherwise they are left running. Nothing else is touched.
    void quit (bool stopChildren);

private:
    struct Started
    {
        Role role;
        std::unique_ptr<a3core::Child> child;
    };

    const a3core::Platform& platform;
    CoreClient& core;
    RemoteHostView& engine;
    std::vector<Started> started;
    bool asked = false;
};

// "Stop Core and the engine too?" -- naming only the roles in `children`.
juce::String stopQuestion (const std::vector<Role>& children);
}
