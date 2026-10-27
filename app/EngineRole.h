// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// `a3-core --role engine`: Plan 4's engine app without its window. Loads the truth, engine.json and
// the spatial data, builds the host on the chosen backend (JACK on the rig, a JUCE device elsewhere),
// listens for Core on engine.osc, says hello to core.osc and drives the host from the app's timer.
// The window's pages reach a running engine over the control API (P2), never from this process.
#pragma once

#include <memory>
#include <optional>
#include <variant>

#include "AppArgs.h"
#include "Roles.h"

namespace a3app
{
class EngineSession;

class EngineRole final : public RoleRunner
{
public:
    // The running engine, or the exit code when the role ends at once: --check-config's answer, or 1
    // for a start that is refused (said on stderr).
    static std::variant<std::unique_ptr<EngineRole>, int> start (const AppArgs&, const juce::File& home);

    ~EngineRole() override;
    std::optional<int> tick (bool stopRequested) override;
    int tickHz() const override { return 100; }   // the /vu sends (25 Hz) are due on a 10 ms grid

private:
    EngineRole (const AppArgs&, StartupData);

    AppArgs args;
    StartupData startup;
    std::unique_ptr<EngineSession> session;
};
}
