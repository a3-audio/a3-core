// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinksCommand.h"

#include <cstdio>
#include <jack/jack.h>

#include "ConfigFile.h"
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

// The truth's level meters; an unreadable truth only costs the VU line its name.
std::vector<std::string> meterNames (const a3core::Platform& platform)
{
    const auto files = coreFiles (platform);
    const auto loaded = a3core::Truth::load (files.truth, files.network);
    if (const auto* truth = std::get_if<a3core::Truth> (&loaded))
        return truth->vuMeters();
    return {};
}

struct EngineMap
{
    AppConfig config;
    bool known = false;
};

EngineMap engineMap (const a3core::Platform& platform)
{
    EngineMap map;
    const auto file = chooseConfigFile ({}, platform.home(), [] (const juce::File& f) { return f.existsAsFile(); });
    juce::String error;
    if (const auto loaded = loadAppConfig (file, error))
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

    const auto map = engineMap (platform);
    std::optional<std::size_t> missing;
    if (map.known)
        missing = linksWithMissingTarget (map.config.io, ports);

    const LinkMeanings meanings { map.config.engine, meterNames (platform) };
    std::fputs (formatLinks ({ links, ports, missing }, meanings).c_str(), stdout);
    return 0;
}
}
