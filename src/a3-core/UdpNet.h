// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Core's datagrams on real sockets. UdpSocket is one IPv4 UDP socket that closes itself; UdpNet is
// the Outbox the service gives Core: one socket per client name, as B1 gave every WatchedClient a
// SimpleUDPClient of its own, and the one socket the hello gate trusts for Core's own replays.
//
// What UdpNet does not do, on purpose: record traffic (Core::send does, once) and say a failure
// (Core's SendFailures does, once per destination). A send that fails throws OsError, as Python's
// sendto raised OSError into WatchedClient; a message that cannot be encoded throws OsError too,
// so it is said once like any other refused send and never dropped without a word.
#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

#include "Net.h"
#include "SendFailures.h"

namespace a3core
{
// B1's a3_core_receive_buffer.WANTED: room for a burst several times over.
inline constexpr int wantedReceiveBuffer = 4 * 1024 * 1024;

class UdpSocket
{
public:
    UdpSocket();   // unbound; the system picks a source port on the first send
    ~UdpSocket();
    UdpSocket (UdpSocket&&) noexcept;
    UdpSocket& operator= (UdpSocket&&) noexcept;
    UdpSocket (const UdpSocket&) = delete;
    UdpSocket& operator= (const UdpSocket&) = delete;

    // Bound to `host` (a dotted IPv4 address, every interface included, or a name) and `port` (0: the system picks), or
    // the OsError Python's bind raised.
    static std::variant<UdpSocket, OsError> bound (const std::string& host, int port);

    int fd() const { return descriptor; }
    Endpoint local() const;   // where it is bound, as getsockname says

    // a3_core_receive_buffer.widen_receive_buffer: ask, and return what the kernel granted
    // (capped by net.core.rmem_max; Linux reports twice the bytes it books).
    int widenReceiveBuffer (int wanted = wantedReceiveBuffer);
    void allowBroadcast();

    // sendto; OsError with errno's text when it fails, or when `to.host` does not resolve.
    void sendTo (const Endpoint& to, const std::vector<uint8_t>& data);

private:
    explicit UdpSocket (int fd) : descriptor (fd) {}
    int descriptor = -1;
};

class UdpNet final : public Outbox
{
public:
    // `self` is the socket Core's replays leave by; the hello gate trusts exactly its endpoint.
    explicit UdpNet (UdpSocket& self) : selfSocket (self) {}

    void send (const Client& client, const OscMessage& message) override;
    // The relayed meters (client "vu"): a failed send throws std::system_error, which the relay's
    // forward() swallows for this destination and the next, as B1's relay passed over OSError.
    void sendRaw (const Client& client, const std::vector<uint8_t>& data) override;

private:
    UdpSocket& socketFor (const std::string& name);

    UdpSocket& selfSocket;
    std::map<std::string, UdpSocket> sockets;
};
}
