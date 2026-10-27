// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A client for the tests of the HTTP server: plain POSIX sockets, so that what is under test is the
// server's bytes and not another JUCE class. Every call has a timeout; none can hang a test.
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace httpclient
{
struct Reply
{
    int status = 0;
    std::map<std::string, std::string> headers;   // names in lower case
    std::string body;
    std::string raw;
};

inline int connectTo (int port, int receiveBuffer = 0)
{
    const int fd = ::socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    if (receiveBuffer > 0)
        ::setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &receiveBuffer, sizeof (receiveBuffer));
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons (static_cast<uint16_t> (port));
    ::inet_pton (AF_INET, "127.0.0.1", &address.sin_addr);
    if (::connect (fd, reinterpret_cast<sockaddr*> (&address), sizeof (address)) != 0)
    {
        ::close (fd);
        return -1;
    }
    return fd;
}

inline void sendAll (int fd, const std::string& text)
{
    size_t done = 0;
    while (done < text.size())
    {
        const auto n = ::send (fd, text.data() + done, text.size() - done, MSG_NOSIGNAL);
        if (n <= 0)
            return;
        done += static_cast<size_t> (n);
    }
}

// Bytes until the peer closes, or `millis` pass.
inline std::string readUntilClosed (int fd, int millis = 5000)
{
    std::string data;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds (millis);
    char buffer[65536];
    while (std::chrono::steady_clock::now() < end)
    {
        pollfd p { fd, POLLIN, 0 };
        if (::poll (&p, 1, 100) <= 0)
            continue;
        const auto n = ::recv (fd, buffer, sizeof (buffer), 0);
        if (n <= 0)
            break;
        data.append (buffer, static_cast<size_t> (n));
    }
    return data;
}

inline Reply parse (const std::string& raw)
{
    Reply reply;
    reply.raw = raw;
    const auto split = raw.find ("\r\n\r\n");
    if (split == std::string::npos)
        return reply;
    const auto head = raw.substr (0, split);
    reply.body = raw.substr (split + 4);
    size_t at = head.find ("\r\n");
    reply.status = head.size() > 9 ? std::atoi (head.c_str() + 9) : 0;
    while (at != std::string::npos)
    {
        const auto next = head.find ("\r\n", at + 2);
        const auto line = head.substr (at + 2, next == std::string::npos ? std::string::npos : next - at - 2);
        const auto colon = line.find (':');
        if (colon != std::string::npos)
        {
            auto name = line.substr (0, colon);
            for (auto& c : name)
                c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
            auto value = line.substr (colon + 1);
            while (! value.empty() && value.front() == ' ')
                value.erase (0, 1);
            reply.headers[name] = value;
        }
        at = next;
    }
    return reply;
}

inline Reply request (int port, const std::string& text)
{
    const int fd = connectTo (port);
    if (fd < 0)
        return {};
    sendAll (fd, text);
    const auto raw = readUntilClosed (fd);
    ::close (fd);
    return parse (raw);
}

inline Reply get (int port, const std::string& path)
{
    return request (port, "GET " + path + " HTTP/1.0\r\n\r\n");
}

inline Reply post (int port, const std::string& path, const std::string& body)
{
    return request (port, "POST " + path + " HTTP/1.0\r\nContent-Length: " + std::to_string (body.size()) + "\r\n\r\n" + body);
}

// The data of the first `count` server-sent events of `path`, then the connection is closed.
inline std::vector<std::string> events (int port, const std::string& path, int count, int millis = 8000)
{
    std::vector<std::string> out;
    const int fd = connectTo (port);
    if (fd < 0)
        return out;
    sendAll (fd, "GET " + path + " HTTP/1.0\r\n\r\n");
    std::string data;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds (millis);
    char buffer[65536];
    bool headDone = false;
    while (static_cast<int> (out.size()) < count && std::chrono::steady_clock::now() < end)
    {
        pollfd p { fd, POLLIN, 0 };
        if (::poll (&p, 1, 100) <= 0)
            continue;
        const auto n = ::recv (fd, buffer, sizeof (buffer), 0);
        if (n <= 0)
            break;
        data.append (buffer, static_cast<size_t> (n));
        if (! headDone)
        {
            const auto split = data.find ("\r\n\r\n");
            if (split == std::string::npos)
                continue;
            data.erase (0, split + 4);
            headDone = true;
        }
        for (auto split = data.find ("\n\n"); split != std::string::npos; split = data.find ("\n\n"))
        {
            out.push_back (data.substr (0, split));
            data.erase (0, split + 2);
        }
    }
    ::close (fd);
    return out;
}

// Whether something can still be connected to on the port.
inline bool connects (int port)
{
    const int fd = connectTo (port);
    if (fd >= 0)
        ::close (fd);
    return fd >= 0;
}
}
