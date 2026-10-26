// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_subscribers.py (B0 f3345dc7feeb): who gets told, and how a new one is added
// without touching the source. Every A3-shaped message goes to all subscribers (the mixer, A3
// Motion, whatever `--subscriber name=host:port` adds); there is no per-message list of
// recipients. This is parsing and a list, no sockets.
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Net.h"
#include "Truth.h"

namespace a3core
{
// The subscriber cannot be understood, so Core will not start: louder than a warning on purpose.
struct SubscriberError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Subscriber
{
    std::string name, host;
    int port;

    bool operator== (const Subscriber& other) const { return name == other.name && host == other.host && port == other.port; }
};

// The two that ship, in the order they are told (the order a recall replays in).
inline const std::vector<std::string> shipped { "mixer", "motion" };

// `name=host:port`; the port is the last colon-separated piece, so an IPv6 host keeps its colons.
Subscriber parseSubscriber (const std::string& text);

// Every `--subscriber`, refusing a name that is already taken. `reserved` is the names Core uses
// for something else.
std::vector<Subscriber> parseSubscribers (const std::vector<std::string>& texts, const std::vector<std::string>& reserved = {});

// The words with a `relayed_to` that Core neither passes on nor replays from the evening: the
// positions go to the pro-DJ apps only, the pointers to one device each and only while LIVE.
bool notPassedOnNorReplayed (const std::string& key);

// Whether a value that just arrived should go on to the other screens: the truth's addresses
// marked `relayed_to`, except those that go elsewhere (the positions, the pointers).
bool relayOnArrival (const Truth& truth, const std::string& address);

// Every subscriber except the one named `origin`; an origin Core cannot name leaves nobody out.
std::vector<Client> everyoneBut (const std::vector<Client>& subscribers, const std::optional<std::string>& origin);

// Put `client` in the slot of the subscriber with its name, in place (std::out_of_range if no
// subscriber has it: adding is --subscriber's job).
void replaceNamed (std::vector<Client>& subscribers, const Client& client);

// Append `client` in place, after the ones that ship (std::invalid_argument if the name is taken).
void addSubscriber (std::vector<Client>& subscribers, const Client& client);

// Take the subscriber called `name` out, in place. Nothing if none is called that.
void dropNamed (std::vector<Client>& subscribers, const std::string& name);

// The one thread allowed to change the subscriber list.
class OwningThread
{
public:
    void claim() { owner = std::this_thread::get_id(); }
    // std::runtime_error naming `what` when nobody claimed or another thread asks.
    void check (const std::string& what) const;

private:
    std::optional<std::thread::id> owner;
};
}
