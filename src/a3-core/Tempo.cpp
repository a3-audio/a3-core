// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tempo.h"

#include <cmath>

namespace a3core
{
std::optional<double> tempoOf (const OscArg& argument)
{
    const auto reading = pythonFloat (argument);
    if (! reading || ! std::isfinite (*reading) || ! (bpmMin <= *reading && *reading <= bpmMax))
        return std::nullopt;
    return reading;
}

std::optional<double> TempoFollower::steady (const OscArg& argument)
{
    const auto reading = tempoOf (argument);
    if (! reading)
        return std::nullopt;

    const double bpm = *reading;
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
    return bpm;
}

std::optional<double> TempoFollower::wanted (const OscArg& argument)
{
    const auto bpm = steady (argument);
    if (! bpm)
        return std::nullopt;

    if (sent && std::abs (*bpm - *sent) < deadband)
        return std::nullopt;

    sent = bpm;
    return bpm;
}
}
