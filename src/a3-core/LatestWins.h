// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Positions: the newest wins (a3_core_latest). Core takes everything waiting on its port and
// handles, per position key, only the last packet; every other packet in its place.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Net.h"

namespace a3core
{
// The position words a packet carries if it carries nothing else (e.g. Motion's bundle of one
// channel's azimuth and elevation), as one key; nullopt for anything else or a damaged packet.
std::optional<std::string> positionKey (const uint8_t* data, size_t size,
                                        const std::function<bool (const std::string&)>& isPosition);

// The indices kept: per key only the last, every packet without a key.
std::vector<size_t> latestWinsIndices (const std::vector<std::optional<std::string>>& keys);

std::vector<std::vector<uint8_t>> latestWins (std::vector<std::vector<uint8_t>> packets,
                                              const std::function<std::optional<std::string> (const std::vector<uint8_t>&)>& keyOf);

struct Datagram
{
    std::vector<uint8_t> data;
    Endpoint from;
};

// Up to `limit` waiting datagrams of a bound UDP socket, without waiting.
std::vector<Datagram> drain (int socketFd, size_t limit);

struct ServeLoop
{
    int socketFd;
    std::function<void (const std::vector<uint8_t>&, const Endpoint&)> handle;
    std::function<std::optional<std::string> (const std::vector<uint8_t>&)> keyOf;
    size_t limit;
    std::function<void (const std::string&)> report;
    std::function<bool()> running;
    std::function<void()> tick;          // optional
    double tickSeconds = 0.1;

    // Another socket read by the same loop, every datagram in its order (nothing collapses): Core's
    // meters' inbox, so its datagrams are handled on the serve thread too (P8).
    struct Side
    {
        int socketFd;
        std::function<void (const std::vector<uint8_t>&, const Endpoint&)> handle;
    };
    std::vector<Side> also = {};
};

// a3_core_latest.serve: wait for the socket, take what waits, hand on what latestWins keeps. A
// handler or tick that throws is reported and the loop goes on. A socket that cannot be waited on
// (closed, or poll() failing) is reported once and ends the loop (Python's select() would raise).
void serve (const ServeLoop& loop);
}
