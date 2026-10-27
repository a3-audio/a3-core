// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_decks.py (B1 7a7b2441f74e): Core's engine-mode deck selector, each channel's
// input ring. A turn moves a cursor, a push commits it, over [deck 1, ..., deck D, ANALOG] -- analog
// last, as the desk reads it: D1 D2 ... A. Channels and decks are numbered as on the wire: channels
// 1-4, decks 1-D (shown as letters A, B, ...), ANALOG = 0.
//
// A deck sits on one channel at most: picking it on a second channel returns the first to ANALOG.
// A deck that drops out stays on its channel, silent; no automatic switch. Core sends the deck its
// 3D while it is on a channel, 1 (neutral) while it is on none, and again on its next hello.
//
// Pure: no OSC sockets, the clock is passed in. Addresses come from the truth.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

#include "Net.h"
#include "OscWire.h"
#include "Truth.h"

namespace a3core
{
// Where deck k listens: stemdeck.osc's port + (k - 1). Two one-deck StemDeck instances on one host
// cannot share a port; the base is the truth's.
int deckPort (const Truth& truth, int deck);
// Deck k on `host`: its own port.
Endpoint deckEndpoint (const Truth& truth, const std::string& host, int deck);

class DeckSelector
{
public:
    static constexpr int analog = 0;
    // A deck is present while its last hello is at most this many seconds old.
    static constexpr double presenceSeconds = 5.0;
    static constexpr double neutralThreeD = 1.0;

    explicit DeckSelector (int channels = 4, int decks = 2);

    int channels() const { return channelCount; }
    int decks() const { return deckCount; }

    // The ring in turn order: decks 1..D, then ANALOG.
    std::vector<int> ring (int channel) const;
    // A place in ring(), not a source; starts on what the channel plays.
    int cursor (int channel) const;
    void turn (int channel, int steps);
    // The changes (channel, source) a push causes; none when the cursor is on what the channel already plays.
    std::vector<std::pair<int, int>> push (int channel);

    int source (int channel) const;
    bool deckChannel (int channel) const;
    std::optional<int> channelOf (int deck) const;

    void heard (int deck, double now);
    bool present (int deck, double now) const;
    std::optional<double> lastHeardOf (int deck) const;

    // Deck k's 3D, to the deck on `channel`; nothing while the channel is analog.
    std::vector<std::pair<std::string, OscArg>> threeDMessages (const Truth& truth, int channel, double value) const;
    // Neutral 3D for every deck the push that made `changes` took off its channel and put on none. A deck that only
    // moved to another channel is not one: it gets that channel's 3D instead.
    std::vector<std::pair<std::string, OscArg>> leaveMessages (const Truth& truth, const std::vector<std::pair<int, int>>& changes) const;
    std::vector<std::pair<std::string, OscArg>> helloMessages (const Truth& truth, int deck, double channelThreeD) const;

    // {"channel.source": [sources]}: the decks block of the evening.
    juce::var toEvening() const;
    // The remembered sources; anything else -- an old stem cursor, an unknown or doubled deck, a wrong shape -- reads as ANALOG.
    void fromEvening (const juce::var& data);

private:
    void set (int channel, int source);
    std::set<int> held() const;

    int channelCount, deckCount;
    std::vector<int> sources, cursors;
    std::map<int, double> lastHeard;
    std::set<int> heldBefore;
};
}
