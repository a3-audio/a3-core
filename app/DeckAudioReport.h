// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AudioBackend.h"
#include "IoMap.h"
#include <optional>
#include <string>
#include <vector>

namespace a3app
{
class EngineHost;

// Per deck slot 1..decks (index 0 = slot 1): true when the map names links for the slot's ports
// and none of their peers is in `missingPeers`. A slot the map says nothing about is false.
std::vector<bool> deckAudioLinked (const IoMap&, int decks, const std::vector<std::string>& missingPeers);

// What the control tick tells Core: nothing (nullopt) while the backend makes no links or has not
// planned them yet, because an empty missingPeers before the first plan would read as "all live".
std::optional<std::vector<bool>> deckAudioReport (const BackendStatus&, const IoMap&, int decks);

// The control tick's step after the backend's service: hands the per-slot state to the host's hello,
// which sends it to Core with every hello and at once on a change. Nothing while deckAudioReport has none.
void reportDeckAudio (const BackendStatus&, const IoMap&, int decks, EngineHost&);
}
