// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "IoMap.h"
#include <string>
#include <vector>

namespace a3app
{
// Per deck slot 1..decks (index 0 = slot 1): true when the map names links for the slot's ports
// and none of their peers is in `missingPeers`. A slot the map says nothing about is false.
std::vector<bool> deckAudioLinked (const IoMap&, int decks, const std::vector<std::string>& missingPeers);
}
