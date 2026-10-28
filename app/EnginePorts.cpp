// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EnginePorts.h"

namespace a3app
{
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
        for (int i = 1; i <= deckAuxChannels; ++i)
            ports.inputs.push_back (deck + "_aux_" + std::to_string (i));
        for (int i = 1; i <= deckCueChannels; ++i)
            ports.inputs.push_back (deck + "_cue_" + std::to_string (i));
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

std::vector<std::string> retiredDeckPorts (int decks)
{
    std::vector<std::string> names;
    for (int slot = 1; slot <= decks; ++slot)
        for (const char* base : { "_cue", "_aux" })
            addPair (names, "deck" + deckLetters (slot) + base);
    return names;
}

bool isEngineInputPort (const std::string& name)
{
    return name.rfind ("analog_", 0) == 0 || name.rfind ("deck", 0) == 0;
}

namespace
{
void bindZone (PortSlots& slots, a3::ZoneOutputs& zone, const a3::Layout& layout)
{
    zone.tops.assign (layout.topChannels.size(), nullptr);
    zone.sub = nullptr;
    if (layout.hasSub())
        slots.outputs.push_back (&zone.sub);
    for (auto& top : zone.tops)
        slots.outputs.push_back (&top);
}
}

// The same walk as enginePortNames; EnginePorts.SlotsFollowTheNames pins the two together.
PortSlots bindPortSlots (const a3::EngineConfig& config, const a3::SpatialConfig& spatial,
                         a3::EngineInputs& in, a3::EngineOutputs& out)
{
    PortSlots slots;
    in.analog.assign ((size_t) config.analogInputs, nullptr);
    in.decks.assign ((size_t) config.decks, a3::DeckInputs {});
    for (auto& analog : in.analog)
        slots.inputs.push_back (&analog);
    for (auto& deck : in.decks)
    {
        for (auto& channel : deck.bformat)
            slots.inputs.push_back (&channel);
        for (auto& channel : deck.aux)
            slots.inputs.push_back (&channel);
        for (auto& channel : deck.cue)
            slots.inputs.push_back (&channel);
    }

    bindZone (slots, out.main, spatial.main.layout);
    bindZone (slots, out.booth, spatial.booth.layout);
    for (auto* pair : { &out.rec, &out.phones })
        for (auto& side : *pair)
        {
            side = nullptr;
            slots.outputs.push_back (&side);
        }
    out.pre.assign ((size_t) config.channels, { nullptr, nullptr });
    out.post.assign ((size_t) config.channels, { nullptr, nullptr });
    for (size_t c = 0; c < (size_t) config.channels; ++c)
        for (auto* taps : { &out.pre, &out.post })
            for (auto& side : (*taps)[c])
                slots.outputs.push_back (&side);
    return slots;
}
}
