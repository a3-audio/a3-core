// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinkPlan.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <map>
#include <tuple>

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

// For each name the map may want, the port of the graph that is it: the port of that name, or one pipewire-jack renamed
// to "<client>-<id>:<port>" (pipewire-jack.c 1.6.9, registry_event_global: a node that appears while another of its
// name exists gets its global id as a suffix and keeps it for good). The suffix is no age: PipeWire reuses freed
// ids. With ages (the ports' serials) the newest candidate wins; without, the plain name, then the highest suffix.
class GraphPorts
{
public:
    GraphPorts (const std::vector<std::string>& ports, const PortAges& ages)
    {
        for (const auto& port : ports)
        {
            const auto age = ageOf (ages, port);
            offer (port, { age, true, 0, port });
            if (const auto rename = renameOf (port); ! rename.original.empty())
                offer (rename.original, { age, false, rename.suffix, port });
        }
    }

    // The port that is the wanted one, or "".
    std::string resolve (const std::string& wanted) const
    {
        const auto found = best.find (wanted);
        return found != best.end() ? found->second.port : std::string();
    }

private:
    struct Candidate
    {
        std::uint64_t age;
        bool plain;
        unsigned long suffix;
        std::string port;
        bool beats (const Candidate& other) const
        {
            return std::tie (age, plain, suffix) > std::tie (other.age, other.plain, other.suffix);
        }
    };
    struct Rename { std::string original; unsigned long suffix = 0; };

    static std::uint64_t ageOf (const PortAges& ages, const std::string& port)
    {
        const auto found = ages.find (port);
        return found != ages.end() ? found->second : 0;
    }

    static Rename renameOf (const std::string& port)
    {
        const auto colon = port.find (':');
        if (colon == std::string::npos)
            return {};
        const auto dash = port.rfind ('-', colon);
        if (dash == std::string::npos || dash == 0 || dash + 1 == colon)
            return {};
        const auto digits = port.substr (dash + 1, colon - dash - 1);
        if (digits.size() > 9 || ! std::all_of (digits.begin(), digits.end(), [] (unsigned char c) { return std::isdigit (c) != 0; }))
            return {};
        return { port.substr (0, dash) + port.substr (colon), std::stoul (digits) };
    }

    void offer (const std::string& name, Candidate candidate)
    {
        const auto known = best.find (name);
        if (known == best.end() || candidate.beats (known->second))
            best[name] = std::move (candidate);
    }

    std::map<std::string, Candidate> best;
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
    return client.rfind ("StemDeck", 0) == 0 || client == "beat-analyzer" || client == "a3-screencast";
}

bool isInputPort (const std::string& ownPort, const std::vector<std::string>& ownInputs)
{
    return std::find (ownInputs.begin(), ownInputs.end(), ownPort) != ownInputs.end();
}

ConnectionPlan planConnections (const LinkMap& wanted, const std::vector<Link>& live, const std::vector<Link>& madeByUs,
                                const std::vector<std::string>& existingPorts, const std::vector<std::string>& ownInputs,
                                const std::string& client, const PortAges& ages)
{
    const GraphPorts graph (existingPorts, ages);

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
    const GraphPorts graph (existingPorts, {});
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
