// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinkPlan.h"
#include <algorithm>
#include <cerrno>
#include <set>

namespace a3::links
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

std::vector<Link> wantedLinks (const LinkMap& wanted, const std::vector<std::string>& ownInputs, const std::string& client)
{
    std::vector<Link> links;
    for (const auto& [port, peers] : wanted.jack)
    {
        const auto own = client + ":" + port;
        for (const auto& peer : peers)
            links.push_back (isInputPort (port, ownInputs) ? Link { peer, own } : Link { own, peer });
    }
    for (const auto& [from, to] : wanted.directLinks)
        links.push_back ({ from, to });
    return links;
}
}

bool linksItself (const std::string& port)
{
    const auto client = port.substr (0, port.find (':'));
    return client.rfind ("StemDeck", 0) == 0 || client == "beat-analyzer";
}

bool isInputPort (const std::string& ownPort, const std::vector<std::string>& ownInputs)
{
    return std::find (ownInputs.begin(), ownInputs.end(), ownPort) != ownInputs.end();
}

ConnectionPlan planConnections (const LinkMap& wanted, const std::vector<Link>& live, const std::vector<Link>& madeByUs,
                                const std::vector<std::string>& existingPorts, const std::vector<std::string>& ownInputs,
                                const std::string& client)
{
    const std::set<std::string> existing (existingPorts.begin(), existingPorts.end());
    const auto links = wantedLinks (wanted, ownInputs, client);

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
    const auto ownPrefix = client + ":";
    const auto ours = [&] (const std::string& port) { return port.rfind (ownPrefix, 0) == 0; };
    for (const auto& link : live)
    {
        const bool onOurPort = ours (link.from) || ours (link.to);
        const bool theirs = linksItself (link.from) || linksItself (link.to);
        if (onOurPort && ! theirs && ! contains (links, link) && ! contains (plan.remove, link))
            plan.remove.push_back (link);
    }
    return plan;
}

std::size_t linksWithMissingTarget (const LinkMap& wanted, const std::vector<std::string>& existingPorts, const std::string& client)
{
    const std::set<std::string> existing (existingPorts.begin(), existingPorts.end());
    const auto ownPrefix = client + ":";
    const auto strangerMissing = [&] (const std::string& port) {
        return existing.count (port) == 0 && port.rfind (ownPrefix, 0) != 0;
    };
    std::size_t count = 0;
    for (const auto& [port, peers] : wanted.jack)
        for (const auto& peer : peers)
            if (strangerMissing (peer))
                ++count;
    for (const auto& [from, to] : wanted.directLinks)
        if (strangerMissing (from) || strangerMissing (to))
            ++count;
    return count;
}

std::vector<Link> stillOwned (const std::vector<Link>& madeByUs, const std::vector<Link>& live)
{
    std::vector<Link> owned;
    for (const auto& link : madeByUs)
        if (contains (live, link))
            owned.push_back (link);
    return owned;
}

ConnectOutcome connectOutcome (int result)
{
    if (result == 0)
        return ConnectOutcome::owned;
    if (result == EEXIST || result == -EEXIST)
        return ConnectOutcome::foreign;
    return ConnectOutcome::failed;
}

std::vector<std::string> clientNamesOf (const std::vector<std::string>& ports)
{
    std::vector<std::string> clients;
    for (const auto& port : ports)
        addOnce (clients, port.substr (0, port.find (':')));
    return clients;
}
}
