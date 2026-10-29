// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_tempo.py (B0 f3345dc7feeb): pass the tempo on, but not every beat. A delay is
// the one effect where chasing a tempo is audible, so a tempo has to hold still for a bar and
// differ from what the far end already has by more than a deadband.
#pragma once

#include <optional>

#include "OscWire.h"

namespace a3core
{
// How far the tempo has to move before it is worth a message, in BPM.
inline constexpr double defaultDeadband = 0.1;
// How many beats a tempo has to hold still before it counts: one bar, counted in beats.
inline constexpr int defaultSteadyBeats = 4;
// What can be a tempo at all; wider than the analyzer's own range on purpose.
inline constexpr double bpmMin = 20.0;
inline constexpr double bpmMax = 400.0;

// The value as a tempo: a number, finite, within bpmMin..bpmMax; nothing otherwise.
std::optional<double> tempoOf (const OscArg& bpm);

// What to pass on, given what keeps arriving. The deadband is measured against what was *sent*,
// so a slow drift cannot creep past it a hundredth at a time.
class TempoFollower
{
public:
    explicit TempoFollower (double deadband = defaultDeadband, int steadyBeats = defaultSteadyBeats)
        : deadband (deadband), steadyBeats (steadyBeats) {}

    // The tempo to send on, or nothing (Python's NO_CHANGE). A value that is not a tempo is
    // refused, and refusing it changes nothing: not what the deadband measures against, not the
    // run of steady beats.
    std::optional<double> wanted (const OscArg& bpm);
    // The tempo once it has held still for a bar, whatever was sent; nothing before. A value that is
    // not a tempo is refused as in wanted(). TempoSource measures the deadband itself, across sources.
    std::optional<double> steady (const OscArg& bpm);

private:
    double deadband;
    int steadyBeats;
    std::optional<double> sent, candidate;
    int run = 0;
};
}
