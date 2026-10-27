// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_state.StateFile (B0 f3345dc7feeb): a state on disk, debounced going out and
// forgiving coming in. Tick-driven (P8): there is no timer thread -- the serve thread offers each
// state with `remember` and asks `flushIfDue` on its tick, with the time it reads from the Clock.
#pragma once

#include <optional>

#include <juce_core/juce_core.h>

namespace a3core
{
class StateFile
{
public:
    // How long a change waits for the next one before it is written: a hand sweeping a knob is one
    // intention, and a file write in the path of every OSC message is one in the path of the music.
    static constexpr double defaultDelay = 2.0;

    explicit StateFile (juce::File path, double delay = defaultDelay);

    // What was remembered, or an empty object: a file that is missing, unreadable, not UTF-8 or
    // half-written is an empty state. Core then comes up on its defaults, as before there was a file.
    // A file that holds some other JSON value gives that value.
    juce::var load() const;

    // Note a state and start the clock on writing it, if it is new. A state equal to the last one
    // offered is not a change: every message offers one and almost none change it, and restarting
    // the clock on each would push the write past the end of a fader sweep every time. The state is
    // copied, so a caller that changes its object afterwards is offering a new state next time.
    //
    // A NaN held is no change (B1: Python's == between two states holding the same float object is
    // True, a container compares by identity first); `freshNan` says a NaN arrived anew -- another
    // object in Python -- so this state is a change whatever it holds.
    void remember (const juce::var& state, double now, bool freshNan = false);

    // Write if a state waits and its time has come.
    void flushIfDue (double now);

    // Write now, if a state waits. `<name>.new` first, then moved into place: a crash in between
    // leaves the old file whole. A write that cannot be made is dropped, as in the Python Core, where
    // it ended the timer thread and nothing else; `writes` does not count it.
    void flush();

    // So a test can say "that burst was one write".
    int writes() const { return written; }

private:
    bool write (const juce::var& state) const;

    juce::File path;
    double delay;
    std::optional<juce::var> pending, last;
    double dueAt = 0;
    int written = 0;
};
}
