// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_stems.py (B1 7a7b2441f74e): the mirror of StemDeck's bus switches, and the desk's
// input selectors on it. StemDeck owns the switches; Core keeps what it last reported, one mask per
// stem pair (bit b-1 = bus b: 1-4 the desk channels, 5 AUX). Core applies its commands to the mirror
// at once; StemDeck's report overwrites whatever Core expected.
//
// Each channel encoder is an input selector over three positions on channel n: deck 1's stem n,
// deck 2's stem n, A (the channel's analog input). Stem n plays only on channel n or on AUX; any
// other switch is refused, and one StemDeck still reports is taken off by tidy() onto AUX.
//
// A channel has a source, STEM or ANALOG, and it is Core's own (maintainer, 2026-10-14): nothing
// StemDeck reports changes it. A channel with no stored source is UNDECIDED (nullopt, null on disk)
// until a push or StemDeck's first settled report decides it (adoptSources). A push on A makes the
// source ANALOG and sends StemDeck nothing; a push on a stem makes it STEM and the stem the channel's
// only input on its bus (the other deck's stem n goes to AUX), and without StemDeck changes nothing.
//
// A channel plays one stem at most, a stem plays on one channel at most, and a stem taken off its
// channel goes to AUX, in either return mode. A stem StemDeck reports on no bus at all is left
// there: its Auto-DJ holds stems off the buses until their switch. Pure: no OSC.
#pragma once

#include <array>
#include <optional>
#include <tuple>
#include <vector>

#include <juce_core/juce_core.h>

namespace a3core
{
// One switch for StemDeck: stem `pair` (1-8) on bus `bus` (1-5), on or off.
struct Command
{
    int pair, bus;
    bool on;

    bool operator== (const Command& other) const { return pair == other.pair && bus == other.bus && on == other.on; }
    bool operator< (const Command& other) const
    {
        return std::tie (pair, bus, on) < std::tie (other.pair, other.bus, other.on);
    }
};

// Channel `index`'s selector positions in turn order: D1 stem n, D2 stem n, A.
std::array<int, 3> ring (int index);
// The channel index 0-3 a stem (pair 1-8) may play on.
int ownChannel (int pair);

class Stems
{
public:
    static constexpr int pairs = 8;
    static constexpr int channels = 4;
    static constexpr int returnIndex = 4;   // the aux return, as a place and as a selector index
    static constexpr int aux = 5;
    static constexpr int stemsPerDeck = 4;
    // A channel selector's cursor: 0-7 the stem pairs 1-8 (cursor = pair - 1), then A.
    static constexpr int analogInput = pairs;
    // A channel's source: which of its two sends is open. nullopt = undecided.
    static constexpr int analogSource = 0, stemSource = 1;
    // The aux return's two modes; its cursor runs over both and the CUE field, a switch of its own.
    static constexpr int analogMode = 0, stemMode = 1, cueField = 2;

    std::array<int, pairs> masks {};
    std::array<int, channels> cursors { analogInput, analogInput, analogInput, analogInput };
    std::array<std::optional<int>, channels> sources {};
    int returnCursor = stemMode;
    int returnMode = stemMode;

    // StemDeck's word for one stem; false for a damaged one. The rules are applied later (tidy),
    // once the reports have settled. A mask may be six bits wide (an older StemDeck's CUE); that bit is dropped.
    bool report (int pair, int mask);
    // StemDeck is gone: nothing is on any bus.
    void forget();

    int channelMask (int index) const;
    bool playsOnReturn (int pair) const;
    // The channel index 0-3 a stem plays on, returnIndex, or none.
    std::optional<int> placeOf (int pair) const;
    // Decide every undecided channel from the buses as they are now; a decided source is never touched.
    void adoptSources();

    void turn (int index, int steps);
    // The commands to StemDeck a push on place `index` means.
    std::vector<Command> push (int index, bool connected = true);
    // The commands that make the mirror obey the rules, applied at once. A second call returns none.
    std::vector<Command> tidy();

    // The cursors, sources and return mode as they are written down.
    juce::var asData() const;
    // The remembered cursors, sources and return mode, or the defaults; never fails.
    static Stems fromData (const juce::var& data);

private:
    bool isOn (int pair, int bus) const;
    std::vector<Command> select (int index, int pair);
    std::vector<Command> toAux (int pair);
    void apply (int pair, int bus, bool on);
    void applyAll (const std::vector<Command>& commands);
};
}
