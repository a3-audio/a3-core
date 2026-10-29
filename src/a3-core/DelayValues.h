// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The four values of the AUX-RTN delay page as Core holds them: an integer step index each, so a
// value is always an exact step (0.1 from index 1, never a sum of 0.1s). A desk turn moves an
// index by its detents; the index is clamped at its ends, it does not wrap. The beat Core keeps is
// the one selected; the one it announces is the one that plays (R16, a3::delay::fitBeatsToLine).
#pragma once

#include <array>
#include <optional>
#include <string>

namespace a3core
{
enum class DelayWord { beat, feedback, color, spread };
inline constexpr std::array<DelayWord, 4> delayWords { DelayWord::beat, DelayWord::feedback, DelayWord::color, DelayWord::spread };

// The truth keys of a word's turn (desk to Core) and of its value (Core to engine and desk), and the
// name its value is kept under in the state file.
const char* delayTurnKey (DelayWord word);
const char* delayValueKey (DelayWord word);
const char* delayStateKey (DelayWord word);
std::optional<DelayWord> delayWordOfTurnKey (const std::string& key);

struct DelaySteps
{
    // Indexes into a3::delay::legalBeats, feedback in 0.05, color in 0.1 (signed, 0 is the centre),
    // spread in 15 degrees. The defaults are checked against a3::delay's in DelayValues.cpp.
    int beat = 1;
    int feedback = 14;
    int color = 0;
    int spread = 4;

    int& at (DelayWord word);
    int at (DelayWord word) const;
    bool operator== (const DelaySteps& other) const
    {
        return beat == other.beat && feedback == other.feedback && color == other.color && spread == other.spread;
    }
};

// Moves a word's step by `clicks` detents (+ clockwise), clamped at the ends. COLOR: a turn that
// would cross the centre stops on it (R11).
void turnDelay (DelaySteps& steps, DelayWord word, long long clicks);

// The word's selected value on the wire: beats, a share, -1..1, degrees.
double delayValue (const DelaySteps& steps, DelayWord word);

// A stored value onto the nearest legal step; a value that is no number leaves the step alone.
void setDelayFromValue (DelaySteps& steps, DelayWord word, double value);

// What the desk is told for BEAT: the beat that plays at this tempo (none known: the selected one).
double announcedBeat (const DelaySteps& steps, std::optional<double> tempo);
}
