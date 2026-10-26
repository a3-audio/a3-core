// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ConnectionPlan.h"
#include "EnginePorts.h"
#include <algorithm>
#include <set>

namespace a3app
{
namespace
{
bool contains (const std::vector<Link>& links, const Link& link)
{
    return std::find (links.begin(), links.end(), link) != links.end();
}

void addOnce (std::vector<std::string>& names, const std::string& name)
{
    if (std::find (names.begin(), names.end(), name) == names.end())
        names.push_back (name);
}

std::vector<Link> wantedLinks (const IoMap& wanted, const std::string& client)
{
    std::vector<Link> links;
    for (const auto& [port, peers] : wanted.jack)
    {
        const auto own = client + ":" + port;
        for (const auto& peer : peers)
            links.push_back (isEngineInputPort (port) ? Link { peer, own } : Link { own, peer });
    }
    for (const auto& [from, to] : wanted.directLinks)
        links.push_back ({ from, to });
    return links;
}
}

ConnectionPlan planConnections (const IoMap& wanted, const std::vector<Link>& live, const std::vector<Link>& madeByUs,
                                const std::vector<std::string>& existingPorts, const std::string& client)
{
    const std::set<std::string> existing (existingPorts.begin(), existingPorts.end());
    const auto links = wantedLinks (wanted, client);

    ConnectionPlan plan;
    for (const auto& link : links)
    {
        const bool fromThere = existing.count (link.from) > 0, toThere = existing.count (link.to) > 0;
        if (! fromThere)
            addOnce (plan.missingPeers, link.from);
        if (! toThere)
            addOnce (plan.missingPeers, link.to);
        if (fromThere && toThere && ! contains (live, link) && ! contains (plan.add, link))
            plan.add.push_back (link);
    }
    for (const auto& link : madeByUs)
        if (contains (live, link) && ! contains (links, link) && ! contains (plan.remove, link))
            plan.remove.push_back (link);
    return plan;
}

std::vector<Link> stillOwned (const std::vector<Link>& madeByUs, const std::vector<Link>& live)
{
    std::vector<Link> owned;
    for (const auto& link : madeByUs)
        if (contains (live, link))
            owned.push_back (link);
    return owned;
}

std::vector<std::string> clientNamesOf (const std::vector<std::string>& ports)
{
    std::vector<std::string> clients;
    for (const auto& port : ports)
        addOnce (clients, port.substr (0, port.find (':')));
    return clients;
}

juce::Result refuseBesideReaper (const std::vector<std::string>& clients, bool connecting)
{
    const bool reaper = std::find (clients.begin(), clients.end(), "REAPER") != clients.end();
    if (connecting && reaper)
        return juce::Result::fail ("REAPER is running: start with --no-connect");
    return juce::Result::ok();
}
}
