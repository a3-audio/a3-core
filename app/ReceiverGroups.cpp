// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReceiverGroups.h"
#include "EnginePorts.h"

namespace a3app
{
namespace
{
// The return comes in on analog 11/12 (the return-sources convention, 2026-10-07: analog 9/10 are the phones).
constexpr int returnStereoFirstPort = 11;

ReceiverGroup analogGroup (const std::string& name, int first)
{
    return { name, { analogPortName (first), analogPortName (first + 1) }, true };
}

void addDeckGroups (std::vector<ReceiverGroup>& groups, int slot)
{
    const auto label = "deck " + deckLetters (slot);
    ReceiverGroup bformat { label + " B-format", {}, false };
    ReceiverGroup aux { label + " return B-format", {}, false };
    for (int i = 1; i <= deckInputChannels; ++i)
        bformat.ports.push_back (deckBFormatPortName (slot, i));
    for (int i = 1; i <= deckAuxChannels; ++i)
        aux.ports.push_back (deckAuxPortName (slot, i));
    groups.push_back (std::move (bformat));
    groups.push_back (std::move (aux));
}
}

std::vector<ReceiverGroup> receiverGroups (const a3::EngineConfig& config)
{
    std::vector<ReceiverGroup> groups;
    for (int slot = 1; slot <= config.decks; ++slot)
        addDeckGroups (groups, slot);
    for (int first = 1; first + 1 <= config.analogInputs; first += 2)
        groups.push_back (analogGroup ("analog " + std::to_string (first) + "-" + std::to_string (first + 1), first));
    if (config.analogInputs >= returnStereoFirstPort + 1)
        groups.push_back (analogGroup ("return stereo", returnStereoFirstPort));
    return groups;
}
}
