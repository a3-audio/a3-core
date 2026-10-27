// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_desks.py: every virtual desk that said hello is followed, four at most, each
// named by its host and listening socket. Pure: the caller passes the clock.
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Net.h"
#include "Presence.h"

namespace a3core
{
inline constexpr int maxDesks = 4;
inline constexpr double deskSilence = 60.0;

// What a followed desk is: the virtual desk, or the pro-DJ app.
inline const std::string deskKind = "desk";
inline const std::string prodjKind = "prodj";

enum class DeskVerdict { joined, kindChanged, known, refusedFirst, refused };

// What the window and relay() call the desk at `host`.
std::string deskName (const std::string& host, const std::string& kind = deskKind);
// The part of a name before the "@"; nullopt for a name without one (Motion's bare "motion").
std::optional<std::string> kindOf (const std::string& name);

class DeskFollowers
{
public:
    DeskFollowers (int deskPort, int deskVuPort, int limit = maxDesks, double silenceAfter = deskSilence);

    // A known host saying another kind takes the new kind and is kindChanged.
    DeskVerdict hello (const std::string& host, double now, const std::string& kind = deskKind);
    // The hosts that have just gone quiet, each once; they are forgotten.
    std::vector<std::string> silence (double now);
    std::vector<std::string> hosts() const;
    std::vector<std::string> hostsOfKind (const std::string& kind) const;
    // deskName(host) when (host, port) is a followed desk's listening socket.
    std::optional<std::string> nameOf (const std::string& host, int port) const;
    std::vector<Endpoint> vuDestinations() const;

    const int port;

private:
    struct Followed
    {
        std::string host;
        Presence presence;
        std::string kind;
    };

    std::vector<Followed>::iterator find (const std::string& host);

    int vuPort, limit;
    double silenceAfter;
    std::vector<Followed> followed;   // in the order they joined
    std::vector<std::string> toldRefused;
    mutable std::mutex lock;
};

// The kind each host last said in a desk hello, followed or not (serve thread only, no lock).
class SaidKinds
{
public:
    explicit SaidKinds (int deskPort) : port (deskPort) {}

    void said (const std::string& host, const std::string& device);
    // `origin` as the LIVE gate reads it.
    std::string gateName (const std::string& origin, const std::string& host, int fromPort) const;

private:
    int port;
    std::vector<std::pair<std::string, std::string>> kinds;
};
}
