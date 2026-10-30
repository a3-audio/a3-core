// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "UdpNet.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>
#include <utility>

namespace a3core
{
namespace
{
OsError lastError() { return OsError (errno, std::strerror (errno)); }

// Python's socket module: a dotted address as it is, anything else through the resolver (whose
// failure is socket.gaierror, an OSError: "[Errno -2] Name or service not known").
sockaddr_in addressOf (const Endpoint& endpoint)
{
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons (static_cast<uint16_t> (endpoint.port));
    if (::inet_pton (AF_INET, endpoint.host.c_str(), &address.sin_addr) == 1)
        return address;
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    if (const auto problem = ::getaddrinfo (endpoint.host.c_str(), nullptr, &hints, &found); problem != 0)
        throw OsError (problem, ::gai_strerror (problem));
    address.sin_addr = reinterpret_cast<sockaddr_in*> (found->ai_addr)->sin_addr;
    ::freeaddrinfo (found);
    return address;
}
}

bool resolvesToIpv4 (const std::string& host)
{
    try
    {
        addressOf ({ host, 0 });
        return true;
    }
    catch (const OsError&)
    {
        return false;
    }
}

UdpSocket::UdpSocket() : descriptor (::socket (AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) {}

UdpSocket::~UdpSocket()
{
    if (descriptor >= 0)
        ::close (descriptor);
}

UdpSocket::UdpSocket (UdpSocket&& other) noexcept : descriptor (std::exchange (other.descriptor, -1)) {}

UdpSocket& UdpSocket::operator= (UdpSocket&& other) noexcept
{
    if (this != &other)
    {
        if (descriptor >= 0)
            ::close (descriptor);
        descriptor = std::exchange (other.descriptor, -1);
    }
    return *this;
}

std::variant<UdpSocket, OsError> UdpSocket::bound (const std::string& host, int port)
{
    UdpSocket made;
    if (made.descriptor < 0)
        return lastError();
    try
    {
        const auto address = addressOf ({ host, port });
        if (::bind (made.descriptor, reinterpret_cast<const sockaddr*> (&address), sizeof address) != 0)
            return lastError();
    }
    catch (const OsError& problem)
    {
        return problem;
    }
    return made;
}

Endpoint UdpSocket::local() const
{
    sockaddr_in address {};
    socklen_t length = sizeof address;
    if (::getsockname (descriptor, reinterpret_cast<sockaddr*> (&address), &length) != 0)
        return {};
    char host[INET_ADDRSTRLEN] = {};
    ::inet_ntop (AF_INET, &address.sin_addr, host, sizeof host);
    return { host, ntohs (address.sin_port) };
}

int UdpSocket::widenReceiveBuffer (int wanted)
{
    ::setsockopt (descriptor, SOL_SOCKET, SO_RCVBUF, &wanted, sizeof wanted);
    int granted = 0;
    socklen_t length = sizeof granted;
    ::getsockopt (descriptor, SOL_SOCKET, SO_RCVBUF, &granted, &length);
    return granted;
}

void UdpSocket::allowBroadcast()
{
    const int on = 1;
    ::setsockopt (descriptor, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
}

void UdpSocket::sendTo (const Endpoint& to, const std::vector<uint8_t>& data)
{
    const auto address = addressOf (to);
    if (::sendto (descriptor, data.data(), data.size(), 0, reinterpret_cast<const sockaddr*> (&address), sizeof address) < 0)
        throw lastError();
}

//==============================================================================

void UdpNet::send (const Client& client, const OscMessage& message)
{
    const auto data = encodeMessage (message);
    if (data.empty())   // where python-osc's builder raises
        throw OsError ("cannot encode the message to '" + message.address + "' as OSC");
    socketFor (client.name).sendTo (client.to, data);
}

void UdpNet::sendRaw (const Client& client, const std::vector<uint8_t>& data)
{
    try
    {
        socketFor (client.name).sendTo (client.to, data);
    }
    catch (const OsError& problem)
    {
        throw std::system_error (std::make_error_code (std::errc::io_error), problem.text);
    }
}

UdpSocket& UdpNet::socketFor (const std::string& name)
{
    if (name == "self")
        return selfSocket;
    return sockets[name];
}
}
