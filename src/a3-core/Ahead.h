// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_ahead.py (B0 f3345dc7feeb): StemDeck's preview of the music
// (/stemdeck/ahead section next bars energy), on its way to Motion. Never noted, never replayed;
// only the StemDeck Core follows may say it.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "OscWire.h"

namespace a3core
{
// The arguments as they go on to Motion, or nothing if they are not a preview. An energy that
// arrived as a whole number goes on as a float, so the wire types stay "ssif".
std::optional<std::vector<OscArg>> aheadArguments (const std::vector<OscArg>& arguments);

// True if a preview from `senderHost` may go on: it is the StemDeck Core follows. None followed
// (no hello yet, or it fell silent): none.
bool fromTheFollowed (const std::string& senderHost, const std::optional<std::string>& followedHost);
}
