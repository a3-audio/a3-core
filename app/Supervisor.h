// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// `a3-core --headless`: Core and the engine as children of this process, for a machine without the
// units (a laptop, a bench). A child that ends is started again one second later; stop() ends both
// (SIGTERM, SIGKILL after a grace). A role whose user unit is enabled is never started here.
#pragma once

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

    Supervisor (const a3core::Platform&, juce::String self, a3core::Journal say = {});
    ~Supervisor();

    void start();
    void tick (double now);
    void stop();

private:
    struct Slot
    {
        Role role;
        std::unique_ptr<a3core::Child> child;
        std::optional<double> restartAt;   // set while the child is down
    };

    void launch (Slot&, double now);
    std::string name (Role) const;

    const a3core::Platform& platform;
    juce::String self;
    a3core::Journal say;
    std::vector<Slot> slots;
    bool stopped = false;
};
}
