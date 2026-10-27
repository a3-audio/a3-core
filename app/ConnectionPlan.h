// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "IoMap.h"
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

namespace a3app
{
// One JACK link, full port names, from the output (source) to the input (sink).
struct Link
{
    std::string from, to;
    bool operator== (const Link& other) const { return from == other.from && to == other.to; }
};

struct ConnectionPlan
{
    std::vector<Link> add, remove;
    std::vector<std::string> missingPeers;   // ports a wanted link needs that the graph does not have now
};

// What the I/O map wants (spec D9): every io.jack entry as a link between the engine's
// port ("client:port") and its peer, in the port's direction, plus every direct link.
// add: a wanted link whose ports both exist and that is not live. remove: a link the
// engine made that is live and no longer wanted. A link the engine did not make is
// never removed, however it touches the engine's ports.
ConnectionPlan planConnections (const IoMap& wanted, const std::vector<Link>& live, const std::vector<Link>& madeByUs,
                                const std::vector<std::string>& existingPorts, const std::string& client = "a3-engine");

// The links the engine still owns: the ones it made that are still live. A link that
// died with a peer's port is no one's; when it is made again it is owned again.
std::vector<Link> stillOwned (const std::vector<Link>& madeByUs, const std::vector<Link>& live);

// What a jack_connect result means for ownership. Only a link the engine itself made is
// owned (0). EEXIST (JACK2 returns it positive, others negated) means someone else made it
// after the graph was read: foreign, not an error. Anything else failed.
enum class ConnectOutcome { owned, foreign, failed };
ConnectOutcome connectOutcome (int jackConnectResult);

// The client names in a list of "client:port" names, each once.
std::vector<std::string> clientNamesOf (const std::vector<std::string>& ports);

// Spec 4b: with connecting on, a client named REAPER beside the engine would put a second
// engine on the PA through the default map; refuse. --no-connect makes no link and runs.
juce::Result refuseBesideReaper (const std::vector<std::string>& clients, bool connecting);
}
