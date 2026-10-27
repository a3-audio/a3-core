// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LinkMap.h"
#include <string>
#include <vector>

namespace a3::links
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

// Whether one of our own ports (no client prefix) takes audio in; that gives a link its direction:
// a peer feeds an input, an output feeds a peer.
bool isInputPort (const std::string& ownPort, const std::vector<std::string>& ownInputs);

// What the map wants (spec D9): every jack entry as a link between our port ("client:port") and
// its peer, in the port's direction, plus every direct link.
// add: a wanted link whose ports both exist and that is not live. remove: a link we made that is
// live and no longer wanted. A link we did not make is never removed, however it touches our ports.
ConnectionPlan planConnections (const LinkMap& wanted, const std::vector<Link>& live, const std::vector<Link>& madeByUs,
                                const std::vector<std::string>& existingPorts, const std::vector<std::string>& ownInputs,
                                const std::string& client = "a3-engine");

// The links we still own: the ones we made that are still live. A link that died with a peer's
// port is no one's; when it is made again it is owned again.
std::vector<Link> stillOwned (const std::vector<Link>& madeByUs, const std::vector<Link>& live);

// What a jack_connect result means for ownership. Only a link we made ourselves is owned (0).
// EEXIST (JACK2 returns it positive, others negated) means someone else made it after the graph
// was read: foreign, not an error. Anything else failed.
enum class ConnectOutcome { owned, foreign, failed };
ConnectOutcome connectOutcome (int jackConnectResult);

// The client names in a list of "client:port" names, each once.
std::vector<std::string> clientNamesOf (const std::vector<std::string>& ports);
}
