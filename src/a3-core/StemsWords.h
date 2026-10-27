// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of the live part of a3_core_stems_reaper.py (B1 7a7b2441f74e; plan ruling P1): the stem
// mirror as messages -- the desk's announcements, the settle that waits for StemDeck's reports to
// quiet down, and the switch commands to StemDeck. The REAPER sends (analog, stems and return
// source) are not here: the engine has no REAPER tracks.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "OscWire.h"
#include "Recall.h"
#include "Stems.h"
#include "Truth.h"

namespace a3core
{
// The deck (1-2) and stem (1-4) a pair (1-8) is, and back.
std::pair<int, int> deckAndStem (int pair);
int pairOf (int deck, int stem);

// Per channel what plays there and where its selector's cursor stands, then the return's cursor
// followed by what plays on it (one argument each, pairs 1-8), and its mode. A channel on ANALOG
// says 0, the analog input, whatever is on its bus; one on STEM or UNDECIDED says its bus mask --
// 0 too while the bus is empty, which the desk then shows as A although the channel is silent.
// Messages, not pairs: the return's word carries nine arguments, as python-osc sends a list.
std::vector<OscMessage> announcements (const Stems& stems, const Truth& truth);

// True once when `delay` seconds have passed since the last poke: StemDeck reports stem by stem,
// and Core tidies only the settled picture. The caller passes the clock.
class Settle
{
public:
    explicit Settle (double after) : delay (after) {}

    void poke (double now) { last = now; }
    bool due (double now);

private:
    double delay;
    std::optional<double> last;
};

// One switch address per command, 1 or 0.
std::vector<Message> commandMessages (const std::vector<Command>& commands, const Truth& truth);

// The messages whose value differs from what was last sent, in order. `lastSent` is the caller's
// memory; it is updated with the returned pairs, so a second call with the same messages returns
// none. Clear it to force a full send. Values compare as Python's != does (1 == 1.0).
std::vector<Message> changedMessages (const std::vector<Message>& messages, std::map<std::string, OscArg>& lastSent);
}
