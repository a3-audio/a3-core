// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// `a3-core --links`: the JACK/PipeWire connections as a beginner reads them -- grouped by the two programs
// they join, ports collapsed into ranges, each range with what it is for. Pure: the graph comes in as
// names, the text goes out; reading the graph is LinksCommand's job.
#pragma once
#include "EngineConfig.h"
#include "a3-links/LinkPlan.h"
#include <optional>
#include <string>
#include <vector>

namespace a3app
{
using a3::links::Link;

// What the program already knows about its ports: which analog inputs are channels and which the
// return (the engine's own config), and which level meters the truth lists (a3-osc.json vu_meters).
struct LinkMeanings
{
    a3::EngineConfig engine;
    std::vector<std::string> vuMeters;
};

struct LinksInput
{
    std::vector<Link> links;                  // output -> input, full "client:port" names
    std::vector<std::string> ports;           // every port of the graph, in the server's order
    std::optional<std::size_t> missingTargets; // configured links with no port to land on; nullopt: not known
};

std::string formatLinks (const LinksInput&, const LinkMeanings&);
}
