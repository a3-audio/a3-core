// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ConfigFile.h"
#include "EnginePorts.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace a3app
{
// The pages' edits, pure: each returns a config that loadAppConfig would accept, or nullopt
// with the reason. An edit never leaves an I/O entry naming a port the new config lacks.

// The engine's ports for this config (loads its spatial directory).
std::optional<PortNames> portNamesFor (const AppConfig&, juce::String& error);
// The map without the entries that name a port `ports` lacks. Direct links are foreign names
// on both ends and stay.
IoMap prunedTo (const IoMap&, const PortNames& ports);

constexpr int maxChannelsOnPage = 128;
// N channels: the analog pair list follows (a new channel takes the next free pair below the
// return, else it is deck-only).
std::optional<AppConfig> withChannels (const AppConfig&, int channels, juce::String& error);
// The page's bound only (A17): one letter per slot, A..Z. The engine itself keeps no cap (A8).
constexpr int maxDecksOnPage = 26;
// N decks, named in the file from then on: a removed slot's links leave the map, an added slot the
// map names none of gets its local StemDeck's links (A9); the other slots stay as they are.
std::optional<AppConfig> withDecks (const AppConfig&, int decks, juce::String& error);
std::optional<AppConfig> withSpatialDirectory (const AppConfig&, const juce::File& directory, juce::String& error);

enum class TrimZone { main, booth, phones, rec };
std::optional<AppConfig> withTrim (const AppConfig&, TrimZone, float db, juce::String& error);   // refuses outside -90..+6 dB

// "off" or one of a3::headphoneEqModels(); anything else is refused naming it.
std::optional<AppConfig> withHeadphoneEq (const AppConfig&, const juce::String& model, juce::String& error);

// Where a deck slot's 41 inputs come from: its local StemDeck, or the AES67 stream of that deck.
enum class DeckSource { local, aes67 };
// Replaces exactly the slot's 41 entries (deck{X}_1..16, _aux_1..16, _cue_1..9) with literal links;
// every other entry stays.
void setDeckSource (IoMap&, int slotOneBased, DeckSource);
// Each slot 1..decks the map names none of the 41 ports of (no JACK peer, no device channel) gets its
// local StemDeck's links; a slot it names any port of stays as it is.
void linkUnnamedSlotsLocally (IoMap&, int decks);
void linkUnnamedSlotLocally (IoMap&, int slotOneBased);
// The source whose 41 links the slot holds exactly; nullopt when it holds anything else
// (missing, partial, or hand-edited links).
std::optional<DeckSource> deckSourceOf (const IoMap&, int slotOneBased);

bool sameConfig (const AppConfig&, const AppConfig&);

// One engine port as the I/O page shows it. `mapped` is what the config holds (editable);
// `foreign` are live peers the config does not hold (shown, never edited).
struct IoRow
{
    std::string port;
    bool input = false;
    std::vector<std::string> mapped, foreign;
    std::optional<int> deviceChannel;
};
using LiveLinks = std::vector<std::pair<std::string, std::string>>;   // (from, to), as AudioBackend::liveLinks
std::vector<IoRow> ioRows (const AppConfig&, const PortNames&, const LiveLinks&);
}
