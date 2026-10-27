// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LatestWins.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <string>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <algorithm>

namespace a3core
{
namespace
{
// a3_core_latest._addresses: a bundle's message addresses (nested bundles opened), or the one
// message's -- read as osc_message.OscMessage reads it, without asking for a leading '/'.
std::optional<std::vector<std::string>> addressesOf (const uint8_t* data, size_t size)
{
    std::vector<std::string> out;
    if (isBundle (data, size))
    {
        const auto messages = decodePacket (data, size);
        if (! messages)
            return std::nullopt;
        for (const auto& m : *messages)
            out.push_back (m.address);
        return out;
    }
    const auto message = decodeMessage (data, size);
    if (! message)
        return std::nullopt;
    out.push_back (message->address);
    return out;
}

void report (const ServeLoop& loop, const std::exception_ptr& error)
{
    try
    {
        std::rethrow_exception (error);
    }
    catch (const std::exception& e)
    {
        loop.report (e.what());
    }
    catch (...)
    {
        loop.report ("unknown exception");
    }
}
}

std::optional<std::string> positionKey (const uint8_t* data, size_t size,
                                        const std::function<bool (const std::string&)>& isPosition)
{
    auto addresses = addressesOf (data, size);
    if (! addresses || addresses->empty())
        return std::nullopt;
    for (const auto& address : *addresses)
        if (! isPosition (address))
            return std::nullopt;
    std::sort (addresses->begin(), addresses->end());   // tuple(sorted(addresses)); code point order = UTF-8 byte order
    std::string key;
    for (const auto& address : *addresses)
        key += address + '\0';   // no address holds a NUL
    return key;
}

std::vector<size_t> latestWinsIndices (const std::vector<std::optional<std::string>>& keys)
{
    std::map<std::string, size_t> last;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i])
            last[*keys[i]] = i;
    std::vector<size_t> kept;
    for (size_t i = 0; i < keys.size(); ++i)
        if (! keys[i] || last[*keys[i]] == i)
            kept.push_back (i);
    return kept;
}

std::vector<std::vector<uint8_t>> latestWins (std::vector<std::vector<uint8_t>> packets,
                                              const std::function<std::optional<std::string> (const std::vector<uint8_t>&)>& keyOf)
{
    std::vector<std::optional<std::string>> keys;
    for (const auto& p : packets)
        keys.push_back (keyOf (p));
    std::vector<std::vector<uint8_t>> kept;
    for (const auto i : latestWinsIndices (keys))
        kept.push_back (std::move (packets[i]));
    return kept;
}

std::vector<Datagram> drain (int socketFd, size_t limit)
{
    std::vector<Datagram> packets;
    std::vector<uint8_t> buffer (65536);
    while (packets.size() < limit)
    {
        sockaddr_in from {};
        socklen_t fromLength = sizeof from;
        const auto got = ::recvfrom (socketFd, buffer.data(), buffer.size(), MSG_DONTWAIT, (sockaddr*) &from, &fromLength);
        if (got < 0)
        {
            if (errno == EINTR)
                continue;
            break;   // EAGAIN: nothing more waiting
        }
        char host[INET_ADDRSTRLEN] = {};
        ::inet_ntop (AF_INET, &from.sin_addr, host, sizeof host);
        packets.push_back ({ { buffer.begin(), buffer.begin() + got }, { host, ntohs (from.sin_port) } });
    }
    return packets;
}

namespace
{
// One bad packet must not stop Core: what a handler throws is reported, and the loop goes on.
void handleReported (const ServeLoop& loop, const std::function<void (const std::vector<uint8_t>&, const Endpoint&)>& handle,
                     const Datagram& datagram)
{
    try
    {
        handle (datagram.data, datagram.from);
    }
    catch (...)
    {
        report (loop, std::current_exception());
    }
}
}

void serve (const ServeLoop& loop)
{
    const int waitMs = loop.tick ? (int) std::ceil (loop.tickSeconds * 1000.0) : -1;
    std::vector<pollfd> watched { { loop.socketFd, POLLIN, 0 } };
    for (const auto& side : loop.also)
        watched.push_back ({ side.socketFd, POLLIN, 0 });
    while (loop.running())
    {
        if (::poll (watched.data(), watched.size(), waitMs) < 0)
        {
            if (errno == EINTR)
                continue;
            loop.report (std::string ("poll: ") + std::strerror (errno));
            return;
        }
        if (std::any_of (watched.begin(), watched.end(), [] (const pollfd& p) { return (p.revents & POLLNVAL) != 0; }))
        {
            loop.report ("poll: the socket is not open");   // waiting again would spin
            return;
        }
        auto packets = drain (loop.socketFd, loop.limit);
        std::vector<std::optional<std::string>> keys;
        for (const auto& p : packets)
            keys.push_back (loop.keyOf (p.data));
        for (const auto i : latestWinsIndices (keys))
            handleReported (loop, loop.handle, packets[i]);
        for (const auto& side : loop.also)
            for (const auto& datagram : drain (side.socketFd, loop.limit))
                handleReported (loop, side.handle, datagram);
        if (loop.tick)
        {
            try
            {
                loop.tick();
            }
            catch (...)
            {
                report (loop, std::current_exception());
            }
        }
    }
}
}
