// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_vu_relay (B0 f3345dc7feeb): the analyzer's meters passed on, bytes in, the same
// bytes out. Nothing is parsed, so nothing here can be slower than the socket. A port and a thread
// of its own, so 25 bundles a second never queue in front of the desk's commands.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <juce_core/juce_core.h>

#include "Net.h"
#include "Truth.h"

namespace a3core
{
using Datagram = std::vector<uint8_t>;

// Larger than any datagram the analyzer sends; a bundle of 50 meters is under 2 KB.
inline constexpr int maxDatagram = 65535;

// Each packet `receive()` returns goes to every destination `destinations()` names at that
// moment, or nowhere when it names none. `receive` returns nullopt when nothing came in time, so
// `running` is asked again. A send that throws std::system_error is not the end, for this
// destination or the next: a desk that has gone away shows up as an error on a later send.
void forward (const std::function<std::optional<Datagram>()>& receive,
              const std::function<void (const Datagram&, const Endpoint&)>& send,
              const std::function<std::vector<Endpoint>()>& destinations,
              const std::function<bool()>& running);

// Who made a datagram on core.vu-relay (O13): the engine measures the points the analyzer did, StemDeck
// its own 41-50. Told by the first /vu/n of the datagram, bundle or message: the truth's vu_meters
// name number n, and a name starting with "stem_" is StemDeck's. A datagram that is no meter, or a
// number the truth does not name, is unknown.
enum class VuSource { engine, stemdeck, unknown };
VuSource vuSourceOf (const Truth& truth, const uint8_t* data, size_t size);

// The displays an engine datagram goes to besides the followed ones: the endpoints of the truth's
// `core` routes that carry vu (the route's `carries`, or a target whose role is vu) -- what the
// analyzer reached. A desk's port is not among them: the desks are the followed ones, by address.
std::vector<Endpoint> engineVuTargets (const Truth& truth);

// `endpoint` at the end of `endpoints`, unless it is there already (host and port alike).
void appendOnce (std::vector<Endpoint>& endpoints, const Endpoint& endpoint);

// `forward` on real sockets. The packets leave by a socket of their own: the inbox is bound to
// loopback, and loopback cannot reach another machine.
class VuRelayThread
{
public:
    // Binds `listenOn` ("local" = loopback, "any" = every interface; port 0 = any free port).
    // `destinations` is asked on the relay's thread: it must be safe to call from there.
    VuRelayThread (const Endpoint& listenOn, std::function<std::vector<Endpoint>()> destinations);
    ~VuRelayThread();

    bool bound() const;
    int boundPort() const;

    // False, and nothing runs, if the inbox could not be bound.
    bool start();
    bool isRunning() const;
    // Stops the thread and waits for it; harmless to call twice.
    void stop();

private:
    class Loop;
    std::unique_ptr<Loop> loop;
};
}
