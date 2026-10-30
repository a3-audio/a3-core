// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "JackLinker.h"
#include <algorithm>
#include <cstdio>

namespace a3::links
{
namespace
{
// jack_get_ports / jack_port_get_all_connections hand out a null-terminated list the caller frees.
std::vector<std::string> takeNames (const char** names)
{
    std::vector<std::string> list;
    if (names == nullptr)
        return list;
    for (auto** name = names; *name != nullptr; ++name)
        list.emplace_back (*name);
    jack_free (names);
    return list;
}
}

JackLinker::JackLinker (jack_client_t* jackClient, std::string name, std::vector<std::string> inputs)
    : client (jackClient), clientName (std::move (name)), ownInputs (std::move (inputs)) {}

void JackLinker::apply (const LinkMap& map)
{
    const auto live = liveLinks();
    madeByUs = stillOwned (madeByUs, live);
    const auto plan = planConnections (map, live, madeByUs, graphPorts(), ownInputs, clientName);
    for (const auto& link : plan.add)
    {
        switch (connectOutcome (jack_connect (client, link.from.c_str(), link.to.c_str())))
        {
            case ConnectOutcome::owned:
                madeByUs.push_back (link);
                break;
            case ConnectOutcome::foreign:   // made by someone else since the graph was read: theirs (R4)
                break;
            case ConnectOutcome::failed:
                if (reportedFailures.insert ({ link.from, link.to }).second)
                    std::fprintf (stderr, "%s: cannot connect %s -> %s\n", clientName.c_str(), link.from.c_str(), link.to.c_str());
                break;
        }
    }
    for (const auto& link : plan.remove)
        if (jack_disconnect (client, link.from.c_str(), link.to.c_str()) == 0)
            madeByUs.erase (std::remove (madeByUs.begin(), madeByUs.end(), link), madeByUs.end());   // a foreign one is not in it
    missing = plan.missingPeers;
}

// One that died and was made again by someone else still looks like ours.
void JackLinker::removeOwned()
{
    madeByUs = stillOwned (madeByUs, liveLinks());
    for (const auto& link : madeByUs)
        jack_disconnect (client, link.from.c_str(), link.to.c_str());
    madeByUs.clear();
}

std::vector<std::string> JackLinker::graphPorts() const
{
    return takeNames (jack_get_ports (client, nullptr, nullptr, 0));
}

std::vector<Link> JackLinker::liveLinks() const
{
    std::vector<Link> links;
    for (const auto& from : takeNames (jack_get_ports (client, nullptr, nullptr, JackPortIsOutput)))
        if (auto* port = jack_port_by_name (client, from.c_str()))
            for (auto& to : takeNames (jack_port_get_all_connections (client, port)))
                links.push_back ({ from, std::move (to) });
    return links;
}
}
