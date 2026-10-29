// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Where the delay's tempo comes from (Ruling A4): a live beat clock first, as the TempoFollower
// passes it on; without one the MASTER deck; else the playing deck on the channel with the
// highest fader; else nothing, and the engine keeps the tempo it has. One deadband, measured
// against what was sent, whichever source sent it -- so a switch of source is not a jump.
//
// Pure: no sockets, the clock and the faders are passed in. No deck count: a deck is whatever
// number reported.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "OscWire.h"
#include "Tempo.h"

namespace a3core
{
class TempoSource
{
public:
    // A /beat with a tempo at most this old means the beat clock runs.
    static constexpr double beatLiveSeconds = 3.0;
    // A deck's report counts while it is at most this old (stemdeck.present's window).
    static constexpr double deckFreshSeconds = 5.0;

    // The fader of the channel deck k is on; nothing while it is on no channel.
    using FaderOf = std::function<std::optional<double> (int deck)>;

    explicit TempoSource (double deadband = defaultDeadband, int steadyBeats = defaultSteadyBeats);

    // A /beat's tempo argument: the tempo to send, or nothing.
    std::optional<double> beat (const OscArg& bpm, double now);
    // stemdeck.tempo of deck k (bpm, playing, master). Malformed arguments are refused and change nothing.
    bool deckReport (int deck, const std::vector<OscArg>& args, double now);
    // The decks' turn while no beat clock runs: the chosen deck's tempo, when it is worth a message.
    std::optional<double> fromDecks (double now, const FaderOf& faderOf);

    // What the last look chose: "beat", "master deck k", "deck k" or "held".
    std::string source() const { return chosen; }

private:
    struct DeckTempo
    {
        double bpm;
        bool playing, master;
        double at;
    };
    struct Choice
    {
        int deck;
        bool master;
    };

    bool beatLive (double now) const;
    bool counts (const DeckTempo& report, double now) const;
    std::optional<Choice> choose (double now, const FaderOf& faderOf) const;
    std::optional<double> sendIfMoved (double bpm);

    TempoFollower follower;
    double deadband;
    std::optional<double> lastBeatAt, sent;
    std::map<int, DeckTempo> reports;
    std::string chosen = "held";
};
}
