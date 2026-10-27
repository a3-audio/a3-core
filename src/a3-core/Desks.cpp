// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Desks.h"

#include <algorithm>

namespace a3core
{
std::string deskName (const std::string& host, const std::string& kind) { return kind + "@" + host; }

std::optional<std::string> kindOf (const std::string& name)
{
    const auto at = name.find ('@');
    if (at == std::string::npos)
        return std::nullopt;
    return name.substr (0, at);
}

DeskFollowers::DeskFollowers (int deskPort, int deskVuPort, int limit, double silenceAfter)
    : port (deskPort), vuPort (deskVuPort), limit (limit), silenceAfter (silenceAfter)
{
}

std::vector<DeskFollowers::Followed>::iterator DeskFollowers::find (const std::string& host)
{
    return std::find_if (followed.begin(), followed.end(), [&] (const Followed& f) { return f.host == host; });
}

DeskVerdict DeskFollowers::hello (const std::string& host, double now, const std::string& kind)
{
    const std::lock_guard<std::mutex> guard (lock);
    const auto known = find (host);
    if (known != followed.end())
    {
        known->presence.heard (now);
        if (known->kind == kind)
            return DeskVerdict::known;
        known->kind = kind;
        return DeskVerdict::kindChanged;
    }
    if ((int) followed.size() >= limit)
    {
        if (std::find (toldRefused.begin(), toldRefused.end(), host) != toldRefused.end())
            return DeskVerdict::refused;
        toldRefused.push_back (host);
        return DeskVerdict::refusedFirst;
    }
    Presence presence (silenceAfter);
    presence.heard (now);
    followed.push_back ({ host, presence, kind });
    toldRefused.erase (std::remove (toldRefused.begin(), toldRefused.end(), host), toldRefused.end());
    return DeskVerdict::joined;
}

std::vector<std::string> DeskFollowers::silence (double now)
{
    const std::lock_guard<std::mutex> guard (lock);
    std::vector<std::string> gone;
    for (const auto& f : followed)
        if (! f.presence.present (now))
            gone.push_back (f.host);
    followed.erase (std::remove_if (followed.begin(), followed.end(),
                                    [&] (const Followed& f) { return ! f.presence.present (now); }),
                    followed.end());
    return gone;
}

std::vector<std::string> DeskFollowers::hosts() const
{
    const std::lock_guard<std::mutex> guard (lock);
    std::vector<std::string> out;
    for (const auto& f : followed)
        out.push_back (f.host);
    return out;
}

std::vector<DeskFollowers::Row> DeskFollowers::rows() const
{
    std::lock_guard<std::mutex> guard (lock);
    std::vector<Row> out;
    for (const auto& one : followed)
        out.push_back ({ one.host, one.kind, one.presence.lastHeard() });
    return out;
}

std::vector<std::string> DeskFollowers::hostsOfKind (const std::string& kind) const
{
    const std::lock_guard<std::mutex> guard (lock);
    std::vector<std::string> out;
    for (const auto& f : followed)
        if (f.kind == kind)
            out.push_back (f.host);
    return out;
}

std::optional<std::string> DeskFollowers::nameOf (const std::string& host, int fromPort) const
{
    const std::lock_guard<std::mutex> guard (lock);
    if (fromPort != port)
        return std::nullopt;
    for (const auto& f : followed)
        if (f.host == host)
            return deskName (host, f.kind);
    return std::nullopt;
}

std::vector<Endpoint> DeskFollowers::vuDestinations() const
{
    const std::lock_guard<std::mutex> guard (lock);
    std::vector<Endpoint> out;
    for (const auto& f : followed)
        out.push_back ({ f.host, vuPort });
    return out;
}

void SaidKinds::said (const std::string& host, const std::string& device)
{
    if (device != deskKind && device != prodjKind)
        return;
    for (auto& entry : kinds)
        if (entry.first == host)
        {
            entry.second = device;
            return;
        }
    kinds.emplace_back (host, device);
}

std::string SaidKinds::gateName (const std::string& origin, const std::string& host, int fromPort) const
{
    if (kindOf (origin).has_value() || fromPort != port)
        return origin;
    for (const auto& entry : kinds)
        if (entry.first == host)
            return deskName (host, entry.second);
    return origin;
}
}
