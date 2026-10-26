// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EnginePorts.h"

namespace a3app
{
std::string deckLetters (int slot)
{
    std::string letters;
    for (; slot > 0; slot = (slot - 1) / 26)
        letters.insert (letters.begin(), (char) ('A' + (slot - 1) % 26));
    return letters;
}

namespace
{
void addZone (std::vector<std::string>& out, const std::string& zone, const a3::Layout& layout)
{
    if (layout.hasSub())
        out.push_back (zone + "_sub");
    for (size_t k = 1; k <= layout.topChannels.size(); ++k)
        out.push_back (zone + "_top_" + std::to_string (k));
}

void addPair (std::vector<std::string>& out, const std::string& base)
{
    out.push_back (base + "_L");
    out.push_back (base + "_R");
}
}

PortNames enginePortNames (const a3::EngineConfig& config, const a3::SpatialConfig& spatial)
{
    PortNames ports;
    for (int i = 1; i <= config.analogInputs; ++i)
        ports.inputs.push_back ("analog_" + std::to_string (i));
    for (int slot = 1; slot <= config.decks; ++slot)
    {
        const auto deck = "deck" + deckLetters (slot);
        for (int i = 1; i <= deckInputChannels; ++i)
            ports.inputs.push_back (deck + "_" + std::to_string (i));
        addPair (ports.inputs, deck + "_cue");
        addPair (ports.inputs, deck + "_aux");
    }

    addZone (ports.outputs, "main", spatial.main.layout);
    addZone (ports.outputs, "booth", spatial.booth.layout);
    addPair (ports.outputs, "rec");
    addPair (ports.outputs, "phones");
    for (int n = 1; n <= config.channels; ++n)
    {
        const auto channel = "ch" + std::to_string (n);
        addPair (ports.outputs, channel + "_pre");
        addPair (ports.outputs, channel + "_post");
    }
    return ports;
}
}
