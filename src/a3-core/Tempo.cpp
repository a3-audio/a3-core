// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tempo.h"

#include <cmath>

namespace a3core
{
std::optional<double> TempoFollower::wanted (const OscArg& argument)
{
    const auto reading = pythonFloat (argument);
    if (! reading)
        return std::nullopt;

    const double bpm = *reading;
    if (! std::isfinite (bpm) || ! (bpmMin <= bpm && bpm <= bpmMax))
        return std::nullopt;

    if (! candidate || std::abs (bpm - *candidate) >= deadband)
    {
        // A different tempo: the run starts again, at this one.
        candidate = bpm;
        run = 1;
        return std::nullopt;
    }

    ++run;
    if (run < steadyBeats)
        return std::nullopt;

    if (sent && std::abs (bpm - *sent) < deadband)
        return std::nullopt;

    sent = bpm;
    return bpm;
}
}
