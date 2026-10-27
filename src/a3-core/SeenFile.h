// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_seen (B0 f3345dc7feeb): the list of addresses Core has seen, kept across a restart
// in <state dir>/a3-core/seen.json. The list, not the history and not the values; never on the hot
// path (Traffic::created() is the only thing it watches); a broken file restores nothing and raises
// nothing. Tick-driven (P8), like StateFile: no timer thread -- the serve thread calls writeIfDue.
#pragma once

#include <optional>
#include <string>

#include <juce_core/juce_core.h>

#include "Net.h"
#include "Traffic.h"

namespace a3core
{
// XDG_STATE_HOME if it is set (even to nothing, as Python's os.environ.get), else ~/.local/state.
juce::File stateDirectory (const std::optional<std::string>& xdgStateHome, const juce::File& home);

// The same, as the platform seam has it (environment and home).
juce::File stateDirectory();

// <state dir>/a3-core/seen.json
juce::File seenPath (const juce::File& stateDir);

class SeenFile
{
public:
    // How long the list has to stand still before it is written.
    static constexpr double defaultDelay = 5.0;

    // `clock` gives the wall and monotonic time restore and write convert between; null is the system's.
    SeenFile (juce::File path, Traffic& traffic, double delay = defaultDelay, Clock* clock = nullptr);

    // Put the saved list back into the traffic. Returns how many rows. A file that is missing,
    // half-written, of another version or not a seen list restores nothing; a row that cannot be read
    // is skipped, the others come back.
    int restore();

    // Write the list now. Never throws; `problem` says what went wrong, "" otherwise.
    void write();

    // The serve thread's tick, with the monotonic time: every `delay` it looks at Traffic::created()
    // and writes when the list has changed since the last write and has stood still since the last look
    // (so a burst of nineteen thousand addresses is one file, written after it).
    void writeIfDue (double now);

    int writes = 0;
    std::string problem;

private:
    juce::File path;
    Traffic& traffic;
    double delay;
    Clock* clock;
    std::optional<double> nextLook;
    std::optional<long long> polled, writtenAtCreated;
};
}
