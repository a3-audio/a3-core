// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Subscribers.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "OscWire.h"

namespace a3core
{
namespace
{
// Words with a `relayed_to` that Core neither passes on here nor replays from the evening: the
// positions go to the pro-DJ apps only, the pointers to one device each and only while LIVE.
const std::set<std::string> notPassedOnNorReplayed { "channel.azimuth", "channel.elevation", "stemdeck.pointer",
                                                     "motion.pointer" };

// str.strip(): the whitespace Python knows in ASCII.
std::string stripped (const std::string& text)
{
    const auto space = [] (char c) { return c == ' ' || (c >= '\t' && c <= '\r') || (c >= 0x1c && c <= 0x1f); };
    auto first = std::find_if_not (text.begin(), text.end(), space);
    auto last = std::find_if_not (text.rbegin(), text.rend(), space).base();
    return first < last ? std::string (first, last) : std::string();
}

// repr() of a str: single quotes unless the text has a ' and no ".
std::string repr (const std::string& text)
{
    const bool useDouble = text.find ('\'') != std::string::npos && text.find ('"') == std::string::npos;
    const char quote = useDouble ? '"' : '\'';
    std::string out (1, quote);
    for (const unsigned char c : text)
    {
        if (c == (unsigned char) quote || c == '\\')
            out += std::string ("\\") + (char) c;
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c < 0x20 || c == 0x7f)
        {
            char hex[8];
            std::snprintf (hex, sizeof hex, "\\x%02x", c);
            out += hex;
        }
        else
            out += (char) c;   // printable ASCII, and UTF-8 bytes as they are (Python keeps printable text)
    }
    return out + quote;
}

[[noreturn]] void refuse (const std::string& what) { throw SubscriberError (what); }

// A plain whole number in text (optional sign, then digits), as far as Python's int() is concerned.
bool isWholeNumber (const std::string& text)
{
    const auto digits = stripped (text);
    const size_t from = (! digits.empty() && (digits[0] == '+' || digits[0] == '-')) ? 1 : 0;
    return from < digits.size() && std::all_of (digits.begin() + (long) from, digits.end(), [] (char c) { return c >= '0' && c <= '9'; });
}

// What str(int(text)) says for such a number.
std::string wholeNumberAsPythonSaysIt (const std::string& text)
{
    auto digits = stripped (text);
    const bool negative = digits[0] == '-';
    if (digits[0] == '+' || digits[0] == '-')
        digits.erase (0, 1);
    digits.erase (0, std::min (digits.find_first_not_of ('0'), digits.size() - 1));
    return (negative && digits != "0" ? "-" : "") + digits;
}
}

Subscriber parseSubscriber (const std::string& text)
{
    const auto equals = text.find ('=');
    if (equals == std::string::npos)
        refuse ("--subscriber wants name=host:port, got " + repr (text));

    const auto name = stripped (text.substr (0, equals));
    const auto endpoint = text.substr (equals + 1);
    if (name.empty())
        refuse ("--subscriber has no name: " + repr (text));

    const auto colon = endpoint.rfind (':');
    if (colon == std::string::npos)
        refuse ("--subscriber " + name + " wants host:port, got " + repr (endpoint));

    const auto host = stripped (endpoint.substr (0, colon));
    const auto portText = endpoint.substr (colon + 1);
    if (host.empty())
        refuse ("--subscriber " + name + " has no host -- write 127.0.0.1:PORT");

    const auto port = pythonInt (pyStr (portText));
    if (! port)
    {
        // A whole number too large for a long long is a port out of range, not a missing number.
        if (isWholeNumber (portText))
            refuse ("--subscriber " + name + " port out of range: " + wholeNumberAsPythonSaysIt (portText));
        refuse ("--subscriber " + name + " has no port number: " + repr (portText));
    }

    if (*port < 1 || *port > 65535)
        refuse ("--subscriber " + name + " port out of range: " + std::to_string (*port));

    return { name, host, (int) *port };
}

std::vector<Subscriber> parseSubscribers (const std::vector<std::string>& texts, const std::vector<std::string>& reserved)
{
    std::set<std::string> taken (reserved.begin(), reserved.end());
    std::vector<Subscriber> found;

    for (const auto& text : texts)
    {
        auto subscriber = parseSubscriber (text);
        if (! taken.insert (subscriber.name).second)
            refuse ("there is already a " + subscriber.name);
        found.push_back (std::move (subscriber));
    }
    return found;
}

bool relayOnArrival (const Truth& truth, const std::string& address)
{
    const auto found = truth.match (address);
    if (! found)
        return false;

    const auto& key = found->first;
    const auto* entry = truth.addressEntry (key).getDynamicObject();
    return entry != nullptr && entry->hasProperty ("relayed_to") && notPassedOnNorReplayed.count (key) == 0;
}

std::vector<Client> everyoneBut (const std::vector<Client>& subscribers, const std::optional<std::string>& origin)
{
    std::vector<Client> heard;
    for (const auto& client : subscribers)
        if (! origin || client.name != *origin)
            heard.push_back (client);
    return heard;
}

void replaceNamed (std::vector<Client>& subscribers, const Client& client)
{
    for (auto& existing : subscribers)
    {
        if (existing.name == client.name)
        {
            existing = client;
            return;
        }
    }
    throw std::out_of_range (client.name);
}

void addSubscriber (std::vector<Client>& subscribers, const Client& client)
{
    const auto taken = std::any_of (subscribers.begin(), subscribers.end(),
                                    [&] (const Client& existing) { return existing.name == client.name; });
    if (taken)
        throw std::invalid_argument ("there is already a " + client.name);
    subscribers.push_back (client);
}

void dropNamed (std::vector<Client>& subscribers, const std::string& name)
{
    subscribers.erase (std::remove_if (subscribers.begin(), subscribers.end(),
                                       [&] (const Client& client) { return client.name == name; }),
                       subscribers.end());
}

void OwningThread::check (const std::string& what) const
{
    if (! owner)
        throw std::runtime_error (what + ": no thread owns the subscribers yet");
    if (std::this_thread::get_id() != *owner)
        throw std::runtime_error (what + " on a thread that does not own the subscribers");
}
}
