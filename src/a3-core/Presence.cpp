// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Presence.h"

#include <utility>

namespace a3core
{
bool Presence::present (double now) const
{
    return last.has_value() && now - *last <= silenceAfter;
}

bool Presence::heard (double now)
{
    const bool news = ! present (now);
    last = now;
    countedGone = false;
    return news;
}

bool Presence::gone (double now)
{
    if (! last.has_value() || countedGone || present (now))
        return false;
    countedGone = true;
    return true;
}

HelloWatch::HelloWatch (double silence, std::function<bool (const std::string&)> isLocalHost, bool oneAtATime)
    : silenceAfter (silence), isLocal (std::move (isLocalHost)), oneAtATime (oneAtATime)
{
}

bool HelloWatch::hello (const std::string& host, double now)
{
    forgetTheSilent (now);
    const auto presence = heardFrom.try_emplace (host, silenceAfter).first;
    const bool arrived = presence->second.heard (now);
    if (! followed.has_value())
    {
        followed = host;
        return true;
    }
    if (*followed == host)
        return arrived;
    if (oneAtATime && heardFrom.at (*followed).present (now))
    {
        noteRival (host);
        return false;
    }
    if (isLocalHost (host) && ! isLocalHost (*followed))
        return false;
    if (! isLocalHost (host) && isLocalHost (*followed))
    {
        followed = host;
        return true;
    }
    if (! arrived)
        return false;
    followed = host;
    return true;
}

bool HelloWatch::silence (double now)
{
    if (! followed.has_value() || heardFrom.at (*followed).present (now))
        return false;
    followed.reset();
    return true;
}

std::vector<std::string> HelloWatch::takeRivals()
{
    return std::exchange (unreported, {});
}

void HelloWatch::noteRival (const std::string& host)
{
    if (! rivals.insert (host).second)
        return;
    unreported.push_back (host);
}

void HelloWatch::forgetTheSilent (double now)
{
    for (auto it = heardFrom.begin(); it != heardFrom.end();)
    {
        const bool isFollowed = followed.has_value() && it->first == *followed;
        if (! isFollowed && ! it->second.present (now))
        {
            rivals.erase (it->first);
            it = heardFrom.erase (it);
        }
        else
            ++it;
    }
}
}
