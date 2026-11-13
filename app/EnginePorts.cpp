// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EnginePorts.h"

namespace a3app
{
namespace
{
void addZone (std::vector<std::string>& out, const std::string& zone, const a3::Layout& layout)
{
    for (const auto& channel : a3::zoneChannels (layout))
        out.push_back (zone + "_" + channel.portSuffix);
}

void addPair (std::vector<std::string>& out, const std::string& base)
{
    for (const auto& side : a3::pairChannels())
        out.push_back (base + "_" + side.portSuffix);
}
}

std::string analogPortName (int number) { return "analog_" + std::to_string (number); }

namespace
{
std::string deckPortName (int slot, const std::string& kind, int number)
{
    return "deck" + deckLetters (slot) + "_" + kind + std::to_string (number);
}
}

std::string deckBFormatPortName (int slot, int number) { return deckPortName (slot, "", number); }
std::string deckAuxPortName (int slot, int number) { return deckPortName (slot, "aux_", number); }
std::string deckCuePortName (int slot, int number) { return deckPortName (slot, "cue_", number); }
std::string deckStereoPortName (int slot, int side)
{
    return "deck" + deckLetters (slot) + "_stereo_" + a3::pairChannels()[(size_t) side].portSuffix;
}

PortNames enginePortNames (const a3::EngineConfig& config, const a3::SpatialConfig& spatial)
{
    PortNames ports;
    for (int i = 1; i <= config.analogInputs; ++i)
        ports.inputs.push_back (analogPortName (i));
    for (int slot = 1; slot <= config.decks; ++slot)
    {
        for (int i = 1; i <= deckInputChannels; ++i)
            ports.inputs.push_back (deckBFormatPortName (slot, i));
        for (int i = 1; i <= deckAuxChannels; ++i)
            ports.inputs.push_back (deckAuxPortName (slot, i));
        for (int i = 1; i <= deckCueChannels; ++i)
            ports.inputs.push_back (deckCuePortName (slot, i));
        for (int side = 0; side < deckStereoChannels; ++side)
            ports.inputs.push_back (deckStereoPortName (slot, side));
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
        for (auto& side : deck.stereo)
            slots.inputs.push_back (&side);
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
