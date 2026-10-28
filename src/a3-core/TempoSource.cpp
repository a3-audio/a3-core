// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TempoSource.h"

#include <cmath>

namespace a3core
{
namespace
{
bool isNumber (const OscArg& arg) { return arg.tag == 'f' || arg.tag == 'd' || arg.tag == 'i' || arg.tag == 'h'; }

// An OSC int that is 0 or 1; a float, a text or any other number is no flag.
std::optional<bool> flagOf (const OscArg& arg)
{
    if (arg.tag != 'i' && arg.tag != 'h')
        return std::nullopt;
    const auto whole = pythonInt (arg);
    if (! whole || (*whole != 0 && *whole != 1))
        return std::nullopt;
    return *whole == 1;
}
}

TempoSource::TempoSource (double deadbandToUse, int steadyBeats) : follower (deadbandToUse, steadyBeats), deadband (deadbandToUse) {}

std::optional<double> TempoSource::beat (const OscArg& bpm, double now)
{
    if (! tempoOf (bpm))
        return std::nullopt;
    lastBeatAt = now;
    chosen = "beat";
    const auto steady = follower.steady (bpm);
    if (! steady)
        return std::nullopt;
    return sendIfMoved (*steady);
}

bool TempoSource::deckReport (int deck, const std::vector<OscArg>& args, double now)
{
    if (args.size() != 3 || ! isNumber (args[0]))
        return false;
    const auto bpm = pythonFloat (args[0]);
    const auto playing = flagOf (args[1]);
    const auto master = flagOf (args[2]);
    // 0 is a deck with no track; below that, or not finite, is no report.
    if (! bpm || ! std::isfinite (*bpm) || *bpm < 0.0 || ! playing || ! master)
        return false;
    reports[deck] = { *bpm, *playing, *master, now };
    return true;
}

std::optional<double> TempoSource::fromDecks (double now, const FaderOf& faderOf)
{
    if (beatLive (now))
    {
        chosen = "beat";
        return std::nullopt;
    }
    const auto choice = choose (now, faderOf);
    if (! choice)
    {
        chosen = "held";
        return std::nullopt;
    }
    chosen = (choice->master ? "master deck " : "deck ") + std::to_string (choice->deck);
    return sendIfMoved (reports.at (choice->deck).bpm);
}

bool TempoSource::beatLive (double now) const { return lastBeatAt && now - *lastBeatAt <= beatLiveSeconds; }

bool TempoSource::counts (const DeckTempo& report, double now) const
{
    return now - report.at <= deckFreshSeconds && report.playing && bpmMin <= report.bpm && report.bpm <= bpmMax;
}

// The MASTER deck first; else the deck on the highest fader, a deck on no channel below any on one.
// The map runs in deck order and only a strictly better deck replaces one: ties go to the lower deck.
std::optional<TempoSource::Choice> TempoSource::choose (double now, const FaderOf& faderOf) const
{
    for (const auto& [deck, report] : reports)
        if (report.master && counts (report, now))
            return Choice { deck, true };

    std::optional<int> best;
    std::optional<double> bestFader;
    for (const auto& [deck, report] : reports)
    {
        if (! counts (report, now))
            continue;
        const auto fader = faderOf (deck);
        const bool better = ! best || (fader && (! bestFader || *fader > *bestFader));
        if (better)
        {
            best = deck;
            bestFader = fader;
        }
    }
    if (! best)
        return std::nullopt;
    return Choice { *best, false };
}

std::optional<double> TempoSource::sendIfMoved (double bpm)
{
    if (sent && std::abs (bpm - *sent) < deadband)
        return std::nullopt;
    sent = bpm;
    return bpm;
}
}
