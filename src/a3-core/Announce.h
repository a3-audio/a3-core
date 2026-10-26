// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_announce.py (B0 f3345dc7feeb): Core says where the truth is. Every two seconds,
// by UDP broadcast on the LAN, /core/here with the URL of the joined truth, its fingerprint and a
// boot id that is new with every Core start. A device that hears a fingerprint it does not have
// fetches the truth and restarts with it.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "Net.h"
#include "SendFailures.h"
#include "Truth.h"

namespace a3core
{
inline constexpr double everySeconds = 2.0;

// IPv4's limited broadcast: a constant of the protocol rather than a fact about the rig.
inline const std::string limitedBroadcast = "255.255.255.255";

// The broadcast address of Core's subnet (the network block's "address", an IPv4 interface as
// Python's ipaddress reads it); the limited broadcast for a host route, a bare address or
// anything that does not parse.
std::string broadcastAddress (const juce::var& network);

std::string truthUrl (const Truth& truth);

// Random, not a clock or a host name: only "same as before or not". 16 hex digits
// (secrets.token_hex(8)). Whoever needs ids injects a BootIdSource; this is the real one.
using BootIdSource = std::function<std::string()>;
std::string newBootId();

// The /core/here datagram: url, fingerprint, boot id, all strings.
std::vector<uint8_t> announcement (const Truth& truth, const std::string& bootId);

// Send `packet` to `target` every `every` seconds while `running()`. A failed send -- no network
// yet at boot -- is reported (OsError), the loop goes on.
void announce (const std::function<void (const Endpoint&, const std::vector<uint8_t>&)>& sendTo, const Endpoint& target,
               const std::vector<uint8_t>& packet, double every, const std::function<void (double)>& sleep,
               const std::function<bool()>& running, const std::function<void (const std::string&)>& report);
}
