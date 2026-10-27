// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ConnectionPlan.h"
#include <algorithm>

namespace a3app
{
juce::Result refuseBesideReaper (const std::vector<std::string>& clients, bool connecting)
{
    const bool reaper = std::find (clients.begin(), clients.end(), "REAPER") != clients.end();
    if (connecting && reaper)
        return juce::Result::fail ("REAPER is running: start with --no-connect");
    return juce::Result::ok();
}
}
