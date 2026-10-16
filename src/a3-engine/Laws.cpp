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
