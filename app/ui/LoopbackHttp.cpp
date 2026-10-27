// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LoopbackHttp.h"

#include <chrono>
#include <cstdlib>

namespace a3app
{
std::optional<HttpReply> httpExchange (const std::string& host, int port, const std::string& method, const std::string& path,
                                       const std::string& body, int timeoutMillis)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMillis);
    const auto left = [&] { return (int) std::chrono::duration_cast<std::chrono::milliseconds> (deadline - std::chrono::steady_clock::now()).count(); };

    juce::StreamingSocket socket;
    if (! socket.connect (host, port, timeoutMillis))
        return std::nullopt;
    const auto request = method + " " + path + " HTTP/1.0\r\nHost: " + host + "\r\nContent-Length: " + std::to_string (body.size())
                         + "\r\nConnection: close\r\n\r\n" + body;
    if (socket.write (request.data(), (int) request.size()) != (int) request.size())
        return std::nullopt;

    std::string raw;
    char buffer[16384];
    for (;;)
    {
        if (left() <= 0)
            return HttpReply { 0, {}, true };
        const int ready = socket.waitUntilReady (true, juce::jmin (left(), 100));
        if (ready < 0)
            break;
        if (ready == 0)
            continue;
        const int got = socket.read (buffer, (int) sizeof (buffer), false);
        if (got <= 0)
            break;
        raw.append (buffer, (size_t) got);
    }
    const auto split = raw.find ("\r\n\r\n");
    if (split == std::string::npos || raw.size() < 12)
        return std::nullopt;
    return HttpReply { std::atoi (raw.c_str() + 9), raw.substr (split + 4) };
}

juce::var parsedJson (const std::string& body)
{
    juce::var out;
    if (juce::JSON::parse (juce::String::fromUTF8 (body.c_str(), (int) body.size()), out).failed())
        return {};
    return out;
}
}
