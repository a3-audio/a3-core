// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinksOverview.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace a3app
{
namespace
{
const std::string arrow = " → ", dash = "–";

std::string lower (std::string text)
{
    std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return text;
}

bool startsWith (const std::string& text, const std::string& prefix) { return text.rfind (prefix, 0) == 0; }

std::size_t shownWidth (const std::string& text)   // characters, not bytes: the dash and the arrow are three
{
    return (std::size_t) std::count_if (text.begin(), text.end(), [] (unsigned char c) { return (c & 0xC0) != 0x80; });
}

std::string padded (const std::string& text, std::size_t width) { return text + std::string (width - shownWidth (text), ' '); }

// ---- the ports -----------------------------------------------------------------------------------

// "main_top_3" is the third top: base "main", number 4 (the sub is 1), as the PA's outputs count.
struct Port
{
    std::string client, name, base;
    int number = 0;     // 0: not a numbered port
    bool sides = false; // number 1/2 are L/R
};

std::pair<std::string, std::string> splitName (const std::string& full)
{
    const auto colon = full.find (':');
    return colon == std::string::npos ? std::pair { full, std::string() } : std::pair { full.substr (0, colon), full.substr (colon + 1) };
}

// "bformat_12" -> bformat 12, "cue_R" -> cue 2 with sides, "vu_main_top1" -> vu_main_top 1, "AUX8" -> AUX 8.
void readNumber (Port& port, const std::string& name)
{
    port.base = name;
    if (name.size() > 2 && name[name.size() - 2] == '_' && (name.back() == 'L' || name.back() == 'R'))
    {
        port.base = name.substr (0, name.size() - 2);
        port.number = name.back() == 'L' ? 1 : 2;
        port.sides = true;
        return;
    }
    auto digits = name.size();
    while (digits > 0 && std::isdigit ((unsigned char) name[digits - 1]))
        --digits;
    if (digits == name.size())
        return;
    port.number = std::stoi (name.substr (digits));
    port.base = name.substr (0, digits);
    if (! port.base.empty() && port.base.back() == '_')
        port.base.pop_back();
}

// "deckA_cue_1" -> "deck A" + "cue_1"
bool splitDeck (const std::string& name, std::string& deck, std::string& rest)
{
    if (! startsWith (name, "deck"))
        return false;
    auto letters = std::size_t (4);
    while (letters < name.size() && std::isupper ((unsigned char) name[letters]))
        ++letters;
    if (letters == 4 || letters >= name.size() || name[letters] != '_')
        return false;
    deck = "deck " + name.substr (4, letters - 4);
    rest = name.substr (letters + 1);
    return true;
}

void readEnginePort (Port& port, const std::string& name)
{
    std::string deck, rest;
    if (splitDeck (name, deck, rest))
    {
        readNumber (port, rest);
        port.base = deck + (port.base.empty() ? "" : " " + port.base);
        return;
    }
    for (const char* zone : { "main", "booth" })
    {
        if (name == std::string (zone) + "_sub")
        {
            port.base = zone;
            port.number = 1;
            return;
        }
        if (startsWith (name, std::string (zone) + "_top_"))
        {
            port.base = zone;
            port.number = std::stoi (name.substr (std::string (zone).size() + 5)) + 1;
            return;
        }
    }
    readNumber (port, name);
}

Port readPort (const std::string& full, const std::string& engineClient)
{
    Port port;
    port.name = full;
    auto [client, name] = splitName (full);
    port.client = client;
    if (client == engineClient)
        readEnginePort (port, name);
    else
        readNumber (port, name);
    return port;
}

std::string spaced (std::string text)
{
    std::replace (text.begin(), text.end(), '_', ' ');
    return text;
}

// ---- the clients ---------------------------------------------------------------------------------

constexpr const char* cardName = "Scarlett";

struct Names
{
    std::string engineClient;
    bool cardSeen = false;

    explicit Names (const std::vector<std::string>& ports, const std::string& engine) : engineClient (engine)
    {
        for (const auto& port : ports)
            cardSeen = cardSeen || lower (port).find (lower (cardName)) != std::string::npos;
    }

    std::string label (const std::string& client) const
    {
        if (client == engineClient)
            return "engine";
        if (client == "system" && cardSeen)
            return cardName;
        if (lower (client).find (lower (cardName)) != std::string::npos)
            return cardName;
        if (startsWith (client, "StemDeck-"))
            return "StemDeck " + client.substr (9);
        return client;
    }

    bool isCard (const std::string& client) const { return client == "system" || label (client) == cardName; }
};

// ---- the lines -----------------------------------------------------------------------------------

struct Connection
{
    Port from, to;
    bool engineIsSource = false, engineIsTarget = false;

    const Port& enginePort() const { return engineIsSource ? from : to; }
    const Port& otherPort() const { return engineIsSource ? to : from; }
};

// What a port is for, as one word that a run of links must share to be written as one line.
std::string categoryOf (const Connection& link, const a3::EngineConfig& engine)
{
    const auto& own = link.enginePort();
    const auto& other = link.otherPort();
    if (own.base == "analog")
    {
        const int pair = (own.number + 1) / 2;
        return std::find (engine.analogPairs.begin(), engine.analogPairs.end(), pair) != engine.analogPairs.end() ? "channel" : "";
    }
    if (own.base == "main")
        return "PA";
    if (own.base == "booth" || own.base == "phones")
        return own.base;
    if (own.base == "rec")
        return other.base == "bpm" ? "tempo" : "recording";
    if (startsWith (own.base, "deck "))
        return own.base.find (" cue") != std::string::npos ? "cue" : own.base.find (" aux") != std::string::npos ? "aux" : "B-format";
    if (startsWith (own.base, "ch") && own.number == 0 && (own.base.find ("_pre") != std::string::npos || own.base.find ("_post") != std::string::npos))
        return "channel tap";
    return "";
}

struct Run
{
    std::vector<Connection> links;
    std::string category;
};

std::string rangeText (const std::vector<Port>& ports)
{
    const int first = ports.front().number, last = ports.back().number;
    if (ports.front().number == 0)
        return {};
    if (ports.front().sides)
        return ports.size() == 2 ? "L/R" : first == 1 ? "L" : "R";
    return first == last ? std::to_string (first) : std::to_string (first) + dash + std::to_string (last);
}

std::string portText (const std::vector<Port>& ports)
{
    const auto numbers = rangeText (ports);
    return spaced (ports.front().base) + (numbers.empty() ? "" : " " + numbers);
}

std::string channelsOf (const Run& run, const a3::EngineConfig& engine)
{
    std::set<int> channels;
    std::set<int> sides;
    for (const auto& link : run.links)
    {
        const int number = link.enginePort().number;
        const auto pair = std::find (engine.analogPairs.begin(), engine.analogPairs.end(), (number + 1) / 2);
        channels.insert ((int) (pair - engine.analogPairs.begin()) + 1);
        sides.insert (number % 2 == 1 ? 1 : 2);
    }
    const auto range = channels.size() == 1 ? "channel " + std::to_string (*channels.begin())
                                            : "channels " + std::to_string (*channels.begin()) + dash + std::to_string (*channels.rbegin());
    return range + ", " + (sides.size() == 2 ? "L/R" : *sides.begin() == 1 ? "L" : "R");
}

std::string meaningOf (const Run& run, const a3::EngineConfig& engine)
{
    if (run.category == "channel")
        return channelsOf (run, engine);
    if (run.category == "channel tap")
    {
        const auto& base = run.links.front().enginePort().base;   // "ch2_pre"
        return "channel " + base.substr (2, base.find ('_') - 2) + (base.find ("_pre") != std::string::npos ? ", pre" : ", post");
    }
    return run.category;
}

bool continues (const Run& run, const Connection& next, const std::string& category)
{
    const auto& last = run.links.back();
    return run.category == category && last.from.base == next.from.base && last.to.base == next.to.base
        && last.from.sides == next.from.sides && last.to.sides == next.to.sides && last.from.number > 0 && last.to.number > 0
        && next.from.number == last.from.number + 1 && next.to.number == last.to.number + 1;
}

std::vector<Run> collapse (const std::vector<Connection>& links, const a3::EngineConfig& engine)
{
    std::vector<Run> runs;
    for (const auto& link : links)
    {
        const auto category = link.engineIsSource || link.engineIsTarget ? categoryOf (link, engine) : std::string();
        if (! runs.empty() && continues (runs.back(), link, category))
            runs.back().links.push_back (link);
        else
            runs.push_back ({ { link }, category });
    }
    return runs;
}

struct Line
{
    std::string from, to, meaning;
};

Line lineOf (const Run& run, const a3::EngineConfig& engine)
{
    std::vector<Port> from, to;
    for (const auto& link : run.links)
    {
        from.push_back (link.from);
        to.push_back (link.to);
    }
    return { portText (from), portText (to), meaningOf (run, engine) };
}

bool isMeterFeed (const Connection& link, const std::set<std::string>& meters)
{
    const auto port = link.to.name.substr (link.to.client.size() + 1);
    return startsWith (port, "vu_") && meters.count (port.substr (3)) > 0;
}

// ---- the groups ----------------------------------------------------------------------------------

struct Group
{
    std::string from, to, heading;
    int order = 4;
    std::vector<Connection> links, meterFeeds;
};

int orderOf (bool fromCard, bool toCard, bool fromEngine, bool toEngine)
{
    if (fromCard && toEngine)
        return 0;
    if (fromEngine && toCard)
        return 1;
    if (fromEngine)
        return 2;
    return toEngine ? 3 : 4;
}

std::string formatGroup (const Group& group, const a3::EngineConfig& engine)
{
    std::vector<Line> lines;
    for (const auto& run : collapse (group.links, engine))
        lines.push_back (lineOf (run, engine));
    std::size_t fromWidth = 0, toWidth = 0;
    for (const auto& line : lines)
    {
        fromWidth = std::max (fromWidth, shownWidth (line.from));
        toWidth = std::max (toWidth, shownWidth (line.to));
    }
    std::ostringstream out;
    out << group.heading << "\n";
    for (const auto& line : lines)
    {
        out << "  " << padded (line.from, fromWidth) << arrow;
        out << (line.meaning.empty() ? line.to : padded (line.to, toWidth) + " (" + line.meaning + ")") << "\n";
    }
    if (! group.meterFeeds.empty())
        out << "  " << group.meterFeeds.size() << " ports → VU meters (level meters)\n";
    return out.str();
}
}

std::string formatLinks (const LinksInput& input, const LinkMeanings& meanings)
{
    const std::string engineClient = "a3-engine";
    const Names names (input.ports, engineClient);
    const std::set<std::string> meters (meanings.vuMeters.begin(), meanings.vuMeters.end());
    const auto indexOf = [&] (const std::string& port) {
        return (std::size_t) (std::find (input.ports.begin(), input.ports.end(), port) - input.ports.begin());
    };

    std::vector<Connection> connections;
    for (const auto& link : input.links)
    {
        Connection connection { readPort (link.from, engineClient), readPort (link.to, engineClient) };
        connection.engineIsSource = connection.from.client == engineClient;
        connection.engineIsTarget = connection.to.client == engineClient;
        connections.push_back (connection);
    }
    // The engine's side decides the order of a group, as the server lists its ports: PA before booth before phones.
    std::stable_sort (connections.begin(), connections.end(), [&] (const Connection& a, const Connection& b) {
        const auto key = [&] (const Connection& c) { return indexOf (c.engineIsSource || ! c.engineIsTarget ? c.from.name : c.to.name); };
        return key (a) < key (b);
    });

    std::map<std::pair<std::string, std::string>, Group> groups;
    for (const auto& connection : connections)
    {
        const auto from = names.label (connection.from.client), to = names.label (connection.to.client);
        auto& group = groups[{ from, to }];
        if (group.from.empty())
        {
            group.from = from;
            group.to = to;
            group.heading = from + (names.isCard (connection.from.client) && connection.engineIsTarget ? " inputs" : "") + arrow + to;
            group.order = orderOf (names.isCard (connection.from.client), names.isCard (connection.to.client),
                                   connection.engineIsSource, connection.engineIsTarget);
        }
        (isMeterFeed (connection, meters) ? group.meterFeeds : group.links).push_back (connection);
    }

    std::vector<const Group*> ordered;
    for (const auto& [key, group] : groups)
        ordered.push_back (&group);
    std::stable_sort (ordered.begin(), ordered.end(), [] (const Group* a, const Group* b) { return a->order < b->order; });

    std::ostringstream out;
    if (ordered.empty())
        out << "No connections in the graph.\n";
    for (std::size_t i = 0; i < ordered.size(); ++i)
        out << (i > 0 ? "\n" : "") << formatGroup (*ordered[i], meanings.engine);

    const bool engineThere = std::any_of (input.ports.begin(), input.ports.end(), [&] (const std::string& port) {
        return startsWith (port, engineClient + ":");
    });
    out << "\n";
    if (! engineThere)
        out << "The engine is not running: it has no ports in the graph.\n";
    else if (! input.missingTargets)
        out << "Configured links: the engine's I/O map could not be read, so missing targets are not counted.\n";
    else if (*input.missingTargets == 0)
        out << "Not connected (configured, target missing): none\n";
    else
        out << "Not connected (configured, target missing): " << *input.missingTargets << "  → see the window's STATUS page\n";
    return out.str();
}
}
