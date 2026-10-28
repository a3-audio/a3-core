// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LinkPlan.h"
#include <jack/jack.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace a3::links
{
// Makes the links of a LinkMap in a JACK graph and removes only the ones it made itself.
// Message thread only: every call here is a JACK graph call.
class JackLinker
{
public:
    JackLinker (jack_client_t*, std::string clientName, std::vector<std::string> ownInputs);

    // The plan against the graph as it is now. A link it made that died with a peer's port is
    // forgotten and made again when the port is back. A failure is reported once per link.
    void apply (const LinkMap&);
    // Against the graph as it is now, like apply(): a link of ours that died since is forgotten, not disconnected.
    void removeOwned();

    const std::vector<std::string>& missingPeers() const { return missing; }
    std::vector<std::string> graphPorts() const;
    std::vector<Link> liveLinks() const;

private:
    jack_client_t* client;
    const std::string clientName;
    const std::vector<std::string> ownInputs;
    std::vector<Link> madeByUs;
    std::vector<std::string> missing;
    std::set<std::pair<std::string, std::string>> reportedFailures;   // say a failing link once, not every second
};
}
