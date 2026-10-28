// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinksCommand.h"

#include <cstdio>
#include <optional>
#include <jack/jack.h>

#include "ConfigFile.h"
#include "ConnectionPlan.h"
#include "LinksOverview.h"
#include "ProgramArgs.h"
#include "Truth.h"

namespace a3app
{
namespace
{
std::vector<std::string> takeNames (const char** names)
{
    std::vector<std::string> list;
    if (names == nullptr)
        return list;
    for (auto** name = names; *name != nullptr; ++name)
        list.emplace_back (*name);
    jack_free (names);
    return list;
}

// Audio only: the Midi-Bridge's ports would say nothing a beginner can use.
std::vector<Link> audioLinks (jack_client_t* client)
{
    std::vector<Link> links;
    for (const auto& name : takeNames (jack_get_ports (client, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput)))
    {
        const auto* port = jack_port_by_name (client, name.c_str());
        if (port == nullptr)
            continue;
        for (const auto& peer : takeNames (jack_port_get_all_connections (client, port)))
            links.push_back ({ name, peer });
    }
    return links;
}

// What the overview takes from the truth: the level meters' names and the deck count of a config that
// names none. An unreadable truth costs the VU line its name and the map its reading.
struct TruthFacts
{
    std::vector<std::string> meters;
    std::optional<int> decks;
};

TruthFacts truthFacts (const a3core::Platform& platform)
{
    const auto files = coreFiles (platform);
    const auto loaded = a3core::Truth::load (files.truth, files.network);
    const auto* truth = std::get_if<a3core::Truth> (&loaded);
    if (truth == nullptr)
        return {};
    TruthFacts facts { truth->vuMeters(), std::nullopt };
    try
    {
        facts.decks = truth->indexRange ("stemdeck.deck-hello", "deck").second;
    }
    catch (const a3core::TruthError&)
    {
    }
    return facts;
}

struct EngineMap
{
    AppConfig config;
    bool known = false;
};

EngineMap engineMap (const a3core::Platform& platform, std::optional<int> truthDecks)
{
    EngineMap map;
    if (! truthDecks)
    {
        std::fputs ("a3-core: the truth gives no deck range (stemdeck.deck-hello), so the engine's map is not read\n", stderr);
        return map;
    }
    const auto file = chooseConfigFile ({}, platform.home(), [] (const juce::File& f) { return f.existsAsFile(); });
    juce::String error;
    if (const auto loaded = loadAppConfig (file, error, *truthDecks))
    {
        map.config = *loaded;
        map.known = true;
    }
    else
        std::fprintf (stderr, "a3-core: %s\n", error.toRawUTF8());
    return map;
}
}

int runLinks (const a3core::Platform& platform)
{
    jack_status_t status {};
    auto* client = jack_client_open ("a3-core-links", JackNoStartServer, &status);
    if (client == nullptr)
    {
        std::fputs ("a3-core: no JACK server running, so there are no connections to show\n", stderr);
        return 1;
    }
    const auto ports = takeNames (jack_get_ports (client, nullptr, nullptr, 0));
    const auto links = audioLinks (client);
    jack_client_close (client);

    const auto facts = truthFacts (platform);
    const auto map = engineMap (platform, facts.decks);
    std::optional<std::size_t> missing;
    if (map.known)
        missing = a3::links::linksWithMissingTarget (linkMapFor (map.config, platform.hostname()), ports);

    const LinkMeanings meanings { map.config.engine, facts.meters };
    std::fputs (formatLinks ({ links, ports, missing }, meanings).c_str(), stdout);
    return 0;
}
}
