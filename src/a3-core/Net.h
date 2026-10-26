// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The seams Core's logic talks to the world through: where a message goes, the clock, the journal.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "OscWire.h"

namespace a3core
{
struct Endpoint
{
    std::string host;
    int port;

    bool operator== (const Endpoint& other) const { return host == other.host && port == other.port; }
};

struct Client
{
    std::string name;
    Endpoint to;
};

class Outbox
{
public:
    virtual ~Outbox() = default;
    virtual void send (const Client&, const OscMessage&) = 0;
    virtual void sendRaw (const Client&, const std::vector<uint8_t>&) = 0;
};

class Clock
{
public:
    virtual ~Clock() = default;
    virtual double monotonic() = 0;
    virtual double wall() = 0;
};

// The loopback address, without a dotted literal in the sources.
inline std::string loopbackAddress() { return juce::IPAddress::local().toString().toStdString(); }

using Journal = std::function<void (const std::string& line, bool toStderr)>;
}
