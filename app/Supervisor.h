// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// `a3-core --headless`: Core and the engine as children of this process, for a machine without the
// units (a laptop, a bench). Each child is kept as the engine unit keeps the engine
// (Restart=on-failure, RestartSec=1, StartLimitBurst=5 in 60 s): a failed child starts again one
// second later, a clean end is left alone, and a role that failed its fifth start within a minute is
// given up. stop() ends both (SIGTERM, SIGKILL after a grace). A role whose user unit is enabled is
// never started here.
#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "Platform.h"
#include "ProgramArgs.h"

namespace a3app
{
class Supervisor
{
public:
    static constexpr double restartAfter = 1.0;
    static constexpr double stopGrace = 5.0;   // Core writes its files on SIGTERM
    static constexpr int startLimit = 5;       // starts within startWindow before a failure gives up
    static constexpr double startWindow = 60.0;

    Supervisor (const a3core::Platform&, juce::String self, a3core::Journal say = {});
    ~Supervisor();

    void start (double now);
    // The exit code once nothing is left to run (every role ended cleanly or was given up), else nullopt.
    std::optional<int> finished() const;
    void tick (double now);
    void stop();

private:
    struct Slot
    {
        Role role;
        std::unique_ptr<a3core::Child> child;
        std::optional<double> restartAt;   // set while a failed child waits to start again
        std::deque<double> starts;          // within startWindow
        bool ended = false, gaveUp = false; // ended: will not run again
    };

    void ended (Slot&, int status, double now);

    void launch (Slot&, double now);
    std::string name (Role) const;

    const a3core::Platform& platform;
    juce::String self;
    a3core::Journal say;
    std::vector<Slot> slots;
    bool stopped = false;
};
}
