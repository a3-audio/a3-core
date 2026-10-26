// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Announce.h"

#include <cstdio>
#include <optional>
#include <random>

#include "OscWire.h"
#include "SendFailures.h"

namespace a3core
{
namespace
{
// ipaddress._ip_int_from_string for IPv4: four decimal octets, no leading zeros, nothing else.
std::optional<uint32_t> ipv4Int (const std::string& text)
{
    uint32_t result = 0;
    size_t start = 0;
    for (int octet = 0; octet < 4; ++octet)
    {
        const auto end = octet < 3 ? text.find ('.', start) : text.size();
        if (end == std::string::npos || end == start || end - start > 3)
            return std::nullopt;
        const auto part = text.substr (start, end - start);
        if (part.find_first_not_of ("0123456789") != std::string::npos || (part.size() > 1 && part[0] == '0'))
            return std::nullopt;
        const int value = std::stoi (part);
        if (value > 255)
            return std::nullopt;
        result = (result << 8) | (uint32_t) value;
        start = end + 1;
    }
    if (start - 1 != text.size())   // a fifth octet (or a dot at the end)
        return std::nullopt;
    return result;
}

// ipaddress._prefix_from_ip_int: a netmask has its ones first, then its zeros.
std::optional<int> prefixOfMask (uint32_t mask)
{
    int zeros = 0;
    while (zeros < 32 && ((mask >> zeros) & 1u) == 0)
        ++zeros;
    const int prefix = 32 - zeros;
    const uint64_t leadingOnes = zeros == 32 ? 0 : (uint64_t) (mask >> zeros);
    const uint64_t allOnes = ((uint64_t) 1 << prefix) - 1;
    return leadingOnes == allOnes ? std::optional<int> (prefix) : std::nullopt;
}

// IPv4Interface's prefix part: a prefix length, a netmask, or a host mask.
std::optional<int> prefixOf (const std::string& text)
{
    const bool digits = ! text.empty() && text.find_first_not_of ("0123456789") == std::string::npos;
    if (digits && text.size() <= 3 && std::stoi (text) <= 32)
        return std::stoi (text);

    const auto asAddress = ipv4Int (text);
    if (! asAddress)
        return std::nullopt;
    if (const auto netmask = prefixOfMask (*asAddress))
        return netmask;
    return prefixOfMask (~*asAddress);
}

std::string dotted (uint32_t address)
{
    return std::to_string (address >> 24) + "." + std::to_string ((address >> 16) & 255u) + "."
         + std::to_string ((address >> 8) & 255u) + "." + std::to_string (address & 255u);
}

std::string pythonHostRepr (const std::string& host) { return "'" + host + "'"; }
}

std::string broadcastAddress (const juce::var& network)
{
    auto* object = network.getDynamicObject();
    if (object == nullptr || ! object->hasProperty ("address") || ! object->getProperty ("address").isString())
        return limitedBroadcast;

    const auto text = object->getProperty ("address").toString().toStdString();
    const auto slash = text.find ('/');
    if (slash != std::string::npos && text.find ('/', slash + 1) != std::string::npos)
        return limitedBroadcast;

    const auto address = ipv4Int (text.substr (0, slash));
    const auto prefix = slash == std::string::npos ? std::optional<int> (32) : prefixOf (text.substr (slash + 1));
    if (! address || ! prefix)
        return limitedBroadcast;

    if (*prefix >= 31)
        return limitedBroadcast;

    const uint32_t mask = *prefix == 0 ? 0u : ~0u << (32 - *prefix);
    return dotted ((*address & mask) | ~mask);
}

std::string truthUrl (const Truth& truth)
{
    return "http://" + truth.host ("core") + ":" + std::to_string (truth.port ("core", "web")) + "/api/truth";
}

std::string newBootId()
{
    std::random_device entropy;
    std::string id;
    for (int i = 0; i < 8; ++i)
    {
        char hex[3];
        std::snprintf (hex, sizeof hex, "%02x", (unsigned) (entropy() & 0xffu));
        id += hex;
    }
    return id;
}

std::vector<uint8_t> announcement (const Truth& truth, const std::string& bootId)
{
    return encodeMessage ({ truth.address ("core.here"), { pyStr (truthUrl (truth)), pyStr (truth.fingerprint()), pyStr (bootId) } });
}

void announce (const std::function<void (const Endpoint&, const std::vector<uint8_t>&)>& sendTo, const Endpoint& target,
               const std::vector<uint8_t>& packet, double every, const std::function<void (double)>& sleep,
               const std::function<bool()>& running, const std::function<void (const std::string&)>& report)
{
    while (running())
    {
        try
        {
            sendTo (target, packet);
        }
        catch (const OsError& problem)
        {
            report ("announce to (" + pythonHostRepr (target.host) + ", " + std::to_string (target.port) + "): " + problem.text);
        }
        sleep (every);
    }
}
}
