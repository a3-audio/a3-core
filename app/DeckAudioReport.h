// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AudioBackend.h"
#include "EngineControls.h"
#include "IoMap.h"
#include <optional>
#include <string>
#include <vector>

namespace a3app
{
class EngineHost;

// Per deck slot 1..decks (index 0 = slot 1): true when either of the slot's groups is linked (see deckModes), so a
// deck in stereo mode is as present as one in B-format. A slot the map says nothing about is false.
std::vector<bool> deckAudioLinked (const IoMap&, int decks, const std::vector<std::string>& missingPeers);

// What a deck slot sends, as its links show it (StemDeck's Output setting). Links are the truth, not the signal:
// stereo only when the slot's stereo pair is linked and its B-format is not; both linked or neither: B-format,
// as before the pair existed.
enum class DeckMode { bformat, stereo };
DeckMode deckModeOf (bool bformatLinked, bool stereoLinked);

// Per deck slot 1..decks (index 0 = slot 1). A group of ports (the slot's B-format with its aux and cue, or its
// stereo pair) is linked when the map names peers for it and none of them is in `missingPeers`.
std::vector<DeckMode> deckModes (const IoMap&, int decks, const std::vector<std::string>& missingPeers);

// The control tick's step after the backend's service: each slot's mode into the controls, which the engine reads
// per block. Nothing changes while deckAudioReport has nothing (no links made, or none planned yet).
void applyDeckModes (const BackendStatus&, const IoMap&, int decks, a3::EngineControls&);

// The control tick's step after applyDeckModes: the same modes to the host's hello, which sends them to Core
// (engine.deck-stereo) with every hello and at once on a change. Nothing before the first link plan.
void reportDeckModes (const BackendStatus&, const IoMap&, int decks, EngineHost&);

// What the control tick tells Core: nothing (nullopt) while the backend makes no links or has not
// planned them yet, because an empty missingPeers before the first plan would read as "all live".
std::optional<std::vector<bool>> deckAudioReport (const BackendStatus&, const IoMap&, int decks);

// The control tick's step after the backend's service: hands the per-slot state to the host's hello,
// which sends it to Core with every hello and at once on a change. Nothing while deckAudioReport has none.
void reportDeckAudio (const BackendStatus&, const IoMap&, int decks, EngineHost&);
}
