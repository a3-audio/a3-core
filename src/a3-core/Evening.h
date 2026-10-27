// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_evening.py (B0 f3345dc7feeb): the evening as Core saw it, written down for the
// next start so a power cut does not cost it. `Relayed` holds what was last passed on and that is
// the engine's own last word; it is written along and, at the next start, sent back
// the way a message from the desk travels.
//
// What does not come back is the content of this module: the lamps (status), the toggles, the
// filter mode and the crossfade (state.json keeps them; two copies could disagree), the positions
// (a trajectory writes them without a hand), and everything Core does not set at all.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "Recall.h"
#include "Truth.h"

namespace a3core
{
// The words Core passes on but does not play back: the crossfade is state.json's, the music
// preview is about bars long gone, and the positions and pointers share the relay's own list.
bool notReplayed (const std::string& key);

// An address as the truth names it today: FX send / FX return became aux send / aux return on
// 2026-10-01, wire included. nullopt where the old name has no current one (a channel the truth
// does not have). Any other address is returned as it is.
std::optional<std::string> currentName (const Truth& truth, const std::string& address);

// One value as json writes it: a number, a text, a bool, or null for None.
juce::var jsonOf (const OscArg& arg);

// What Core last passed on, as data json can write: {"values": {address: value}}.
juce::var eveningState (const Relayed& relayed);

// The values of `state` that may be replayed at the start, in file order, under their current
// names. Every shape a file can arrive in is a shape it may arrive in: older, edited by hand,
// empty, no object at all. None of them throws.
std::vector<Message> replayable (const Truth& truth, const juce::var& state);
}
