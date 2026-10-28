// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ConfigFile.h"
#include "a3-links/LinkMap.h"
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

namespace a3app
{
// Spec 4b: with connecting on, a client named REAPER beside the engine would put a second
// engine on the PA through the default map; refuse. --no-connect makes no link and runs.
juce::Result refuseBesideReaper (const std::vector<std::string>& clients, bool connecting);

// P40: rec_L/R -> "AES67 <hostname> out:send_CH1/2" (the name a3-osc-render gives this machine's sender),
// added to whatever map was loaded, so a deck host records the REC bus even from an engine.json saved
// before M10. Applied at link time only: the user's file is never rewritten. No host name, no link.
void addRecToAes67Sender (a3::links::LinkMap&, const std::string& hostname);

// What the engine links and counts: the loaded io map plus the built-in links above. `hostname` is
// Platform::hostname(), the source a3-osc-render names the sender from.
IoMap linkMapFor (const AppConfig&, const std::string& hostname);
}
