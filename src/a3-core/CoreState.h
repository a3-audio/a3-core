// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_state.py (B0 f3345dc7feeb): what Core alone knows, kept across a restart.
//
// Almost nothing needs this: every continuous value passes through Core and is held where it
// lands. What is left exists in Core's head and nowhere else -- the two toggles a channel carries,
// the 3D crossfade (its consequence is held, its cause cannot be read back), the filter mode, the
// phones-mix knob and the aux return's cue. The position is deliberately not here: a trajectory
// changes it continuously, and remembering it would mean writing the file for a whole set.
#pragma once

#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "Channels.h"

namespace a3core
{
// The fields of a channel and of the master this keeps, and nothing else.
inline const std::vector<std::string> channelFields { "toggle_fx", "toggle_cue", "three_d" };
// The delay_* fields are kept beside them, each only while it differs from its default.
inline const std::vector<std::string> masterFields { "phones_mix", "return_cue" };

// Everything worth remembering, as data json can write. `stemsData` (the stem mirror's own
// as_data()) is carried through under "stems" when given.
juce::var stateOf (const std::vector<ChannelInfo>& channels, const MasterInfo& master, const juce::var* stemsData = nullptr);

// Put a remembered state back, leaving alone anything it does not name. A file outlives the version
// that wrote it: one with a field fewer, one with a field more, one from a device with other
// channels, one that is no object at all. None of them may raise and none may clear a value nobody
// asked about -- a state file is a convenience. `toggle_pfl` is read as `toggle_cue` (renamed
// 2026-10-01) unless the file also says `toggle_cue`.
void applyState (const juce::var& state, std::vector<ChannelInfo>& channels, MasterInfo& master);
}
