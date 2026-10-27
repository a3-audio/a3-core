// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MotionTarget.h"

#include <arpa/inet.h>

namespace a3core
{
namespace
{
// How the watch names the rig's own Motion, whichever of this machine's addresses it sent from.
const std::string localKey = "local";

bool isLoopbackAddress (const std::string& host)
{
    in_addr v4 {};
    if (inet_pton (AF_INET, host.c_str(), &v4) == 1)
        return (ntohl (v4.s_addr) >> 24) == 127;
    in6_addr v6 {};
    return inet_pton (AF_INET6, host.c_str(), &v6) == 1 && IN6_IS_ADDR_LOOPBACK (&v6);
}
}

bool isThisMachine (const std::string& host, const std::set<std::string>& ownHosts)
{
    return isLoopbackAddress (host) || ownHosts.count (host) > 0;
}

std::string hostToTellOf (const std::string& host, const std::string& coreHost, const std::set<std::string>& ownHosts)
{
    return isThisMachine (host, ownHosts) ? coreHost : host;
}

MotionTarget::MotionTarget (Endpoint ownTarget, int motionPort, std::set<std::string> ownAddresses,
                            int motionVuPort, double silenceAfter)
    : own (ownTarget), currentTarget (ownTarget), remotePort (motionPort), vuPort (motionVuPort),
      ownHosts (std::move (ownAddresses)),
      watch (silenceAfter, [] (const std::string& host) { return host == localKey; }, true)
{
}

std::optional<Endpoint> MotionTarget::hello (const std::string& host, double now)
{
    const bool local = isThisMachine (host, ownHosts);
    const std::string key = local ? localKey : host;
    seenAs[key] = host;
    if (! watch.hello (key, now))
        return std::nullopt;
    return pointAt (local ? own : Endpoint { host, remotePort });
}

std::optional<std::string> MotionTarget::followed() const
{
    if (! watch.host().has_value())
        return std::nullopt;
    const auto seen = seenAs.find (*watch.host());
    if (seen == seenAs.end())
        return std::nullopt;
    return seen->second;
}

bool MotionTarget::follows (const std::string& host) const
{
    const std::string key = isThisMachine (host, ownHosts) ? localKey : host;
    return watch.host().has_value() && *watch.host() == key;
}

std::vector<std::string> MotionTarget::takeRivals()
{
    std::vector<std::string> out;
    for (const auto& key : watch.takeRivals())
    {
        const auto seen = seenAs.find (key);
        out.push_back (seen == seenAs.end() ? key : seen->second);
    }
    return out;
}

std::optional<Endpoint> MotionTarget::silence (double now)
{
    if (! watch.silence (now))
        return std::nullopt;
    return pointAt (own);
}

std::optional<Endpoint> MotionTarget::vuDestination() const
{
    if (! remote())
        return std::nullopt;
    return Endpoint { currentTarget.host, vuPort };
}

std::optional<Endpoint> MotionTarget::pointAt (const Endpoint& endpoint)
{
    if (endpoint == currentTarget)
        return std::nullopt;
    currentTarget = endpoint;
    return endpoint;
}
}
