// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_motion.py: which Motion Core talks to -- the one that said hello first and is
// still there; a silent remote falls back to the rig's own. Pure: the caller passes the clock.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Net.h"
#include "Presence.h"

namespace a3core
{
// Loopback, or one of the addresses Core knows as its own. A name is compared as written.
bool isThisMachine (const std::string& host, const std::set<std::string>& ownHosts);
// Where a device runs, as told to an app on another machine: a device on this machine is at
// Core's address there, not at loopback.
std::string hostToTellOf (const std::string& host, const std::string& coreHost, const std::set<std::string>& ownHosts);

class MotionTarget
{
public:
    MotionTarget (Endpoint own, int motionPort, std::set<std::string> ownHosts, int motionVuPort,
                  double silenceAfter = motionSilence);

    const Endpoint& current() const { return currentTarget; }
    const Endpoint& defaultTarget() const { return own; }
    bool remote() const { return ! (currentTarget == own); }

    // The new target when this hello moves it, else nullopt.
    std::optional<Endpoint> hello (const std::string& host, double now);
    // The address of the Motion followed, as it was heard; nullopt if none.
    std::optional<std::string> followed() const;
    bool follows (const std::string& host) const;
    // When the Motion followed last said hello; nullopt if none is followed.
    std::optional<double> lastHeard() const;
    // Addresses of Motions that turned up beside the one followed, each once.
    std::vector<std::string> takeRivals();
    // The default, once, when the remote Motion followed has gone quiet.
    std::optional<Endpoint> silence (double now);
    // Where Core forwards the analyzer's meters: the remote Motion's vu port, else nullopt.
    std::optional<Endpoint> vuDestination() const;
    bool forwardsBeat() const { return remote(); }

private:
    std::optional<Endpoint> pointAt (const Endpoint& endpoint);

    Endpoint own, currentTarget;
    int remotePort, vuPort;
    std::set<std::string> ownHosts;
    HelloWatch watch;
    std::map<std::string, std::string> seenAs;
};
}
