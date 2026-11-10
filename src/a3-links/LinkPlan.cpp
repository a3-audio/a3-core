// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinkPlan.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <map>
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

// The ports of the graph, and for a name the graph lacks, the port pipewire-jack renamed to "<client>-<id>:<port>"
// (pipewire-jack.c 1.6.9, :3873: a node that appears while another of its name exists keeps the suffix for good).
class GraphPorts
{
public:
    explicit GraphPorts (const std::vector<std::string>& ports) : exact (ports.begin(), ports.end())
    {
        for (const auto& port : ports)
            addIfRenamed (port);
    }

    // The exact name when the graph has it, else the renamed port with the highest id (the newest), else "".
    std::string resolve (const std::string& wanted) const
    {
        if (exact.count (wanted) > 0)
            return wanted;
        const auto found = newestRename.find (wanted);
        return found != newestRename.end() ? found->second.port : std::string();
    }

private:
    struct Rename { unsigned long id; std::string port; };

    void addIfRenamed (const std::string& port)
    {
        const auto colon = port.find (':');
        if (colon == std::string::npos)
            return;
        const auto dash = port.rfind ('-', colon);
        if (dash == std::string::npos || dash == 0 || dash + 1 == colon)
            return;
        const auto digits = port.substr (dash + 1, colon - dash - 1);
        if (digits.size() > 9 || ! std::all_of (digits.begin(), digits.end(), [] (unsigned char c) { return std::isdigit (c) != 0; }))
            return;
        const auto id = std::stoul (digits);
        const auto original = port.substr (0, dash) + port.substr (colon);
        const auto known = newestRename.find (original);
        if (known == newestRename.end() || id > known->second.id)
            newestRename[original] = { id, port };
    }

    std::set<std::string> exact;
    std::map<std::string, Rename> newestRename;
};

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
    const GraphPorts graph (existingPorts);

    ConnectionPlan plan;
    std::vector<Link> links;
    for (const auto& link : wantedLinks (wanted, ownInputs, client))
    {
        const auto from = graph.resolve (link.from), to = graph.resolve (link.to);
        if (from.empty())
            addOnce (plan.missingPeers, link.from);
        if (to.empty())
            addOnce (plan.missingPeers, link.to);
        const Link resolved { from.empty() ? link.from : from, to.empty() ? link.to : to };
        links.push_back (resolved);
        if (! from.empty() && ! to.empty() && ! contains (live, resolved) && ! contains (plan.add, resolved))
            plan.add.push_back (resolved);
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
    const GraphPorts graph (existingPorts);
    const auto ownPrefix = client + ":";
    const auto strangerMissing = [&] (const std::string& port) {
        return graph.resolve (port).empty() && port.rfind (ownPrefix, 0) != 0;
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
