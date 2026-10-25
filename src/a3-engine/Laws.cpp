// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Laws.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace a3::laws
{
namespace
{
constexpr double filterLowestHz = 20.0;
constexpr double filterSpan = 1000.0;   // 20 Hz .. 20 kHz
constexpr double silenceBelow = 1e-9;   // cos(pi/2) in floating point is 6e-17, not 0
constexpr double halfPi = 1.57079632679489661923;

double clamp01 (double x)
{
    return std::isnan (x) ? 0.0 : std::clamp (x, 0.0, 1.0);
}

// Piecewise-linear lookup over points spaced 0.1 apart from 0, as np.interp
// does with np.arange(0, 1, 0.1). Inputs at or past the last point stay flat.
template <size_t N>
double interpTable (double x, const std::array<double, N>& table)
{
    constexpr double step = 0.1;
    constexpr double last = step * (double) (N - 1);
    if (x >= last)
        return table.back();

    const double position = x / step;
    const size_t segment = std::min ((size_t) position, N - 2);
    const double fraction = position - (double) segment;
    return table[segment] + fraction * (table[segment + 1] - table[segment]);
}
}

double slopeVolume (double control)
{
    return 0.5 * clamp01 (control);
}

double slopeEq (double control)
{
    static constexpr std::array<double, 10> table { 0.0, 0.1, 0.2, 0.3, 0.5, 0.52, 0.54, 0.56, 0.58, 0.6 };
    return interpTable (clamp01 (control), table);
}

double slopeConstantPower (double control)
{
    static constexpr std::array<double, 10> table { 0.0, 0.4, 0.6, 0.7, 0.75, 0.77, 0.8, 0.85, 0.9, 1.0 };
    return interpTable (clamp01 (control), table);
}

double sendGain (double sendValue)
{
    const double value = clamp01 (sendValue);
    if (value < silenceBelow)
        return 0.0;

    return std::pow (10.0, (-40.0 + 40.0 * value) / 20.0);
}

double auxSendGain (double control)
{
    return sendGain (slopeConstantPower (control));
}

double cueWeight (double phonesMix)
{
    return sendGain (std::cos (clamp01 (phonesMix) * halfPi));
}

double mixWeight (double phonesMix)
{
    return sendGain (std::sin (clamp01 (phonesMix) * halfPi));
}

double filterCutoffHz (bool highPass, double control)
{
    const double x = clamp01 (control);
    const double normalised = highPass ? 0.2 + 0.6 * x : 0.8 - 0.6 * x;
    return filterLowestHz * std::pow (filterSpan, normalised);
}

double filterResonance (double control)
{
    return clamp01 (control);
}

bool isSilent (double control)
{
    return clamp01 (control) < silenceBelow;
}

double stageGain (double control)
{
    if (isSilent (control))
        return 0.0;

    return std::pow (10.0, (slopeVolume (control) * 80.0 - 40.0) / 20.0);
}

double bandGain (double threeD)
{
    const double amplitude = clamp01 (threeD);
    if (amplitude <= 0.0)
        return 0.0;

    const double decibels = 20.0 * std::log10 (amplitude);
    const double gain = (decibels + 40.0) / 80.0;
    return std::clamp (gain, 0.0, purestGainUnity);
}

double isolatorParam (double control)
{
    return 0.05 + 0.85 * clamp01 (control);
}
}
