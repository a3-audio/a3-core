// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window's one HTTP client: one request on loopback, a deadline for the whole exchange, the body of
// the answer. Shared by the engine's remote view and Core's client, so both wait and give up alike.
#pragma once

#include <optional>
#include <string>

#include <juce_core/juce_core.h>

namespace a3app
{
struct HttpReply
{
    int status = 0;
    std::string body;
    bool timedOut = false;   // connected and asked, no answer in time (status 0)
};

// nullopt: nothing to connect to, or no HTTP answer at all.
std::optional<HttpReply> httpExchange (const std::string& host, int port, const std::string& method, const std::string& path,
                                       const std::string& body, int timeoutMillis);

// The body as JSON; a void var when it is none.
juce::var parsedJson (const std::string& body);
}
