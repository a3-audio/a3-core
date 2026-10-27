// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DelayModulation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace a3
{
namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr float tempoFloor = 20.0f, tempoCeiling = 400.0f;

// splitmix64: a stateless, reproducible source of targets, so any time can be asked for.
double randomTarget (size_t parameterIndex, int64_t segment)
{
    uint64_t z = (uint64_t) segment * 0x9E3779B97F4A7C15ull + (parameterIndex + 1) * 0xD1B54A32D192ED03ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double) (z >> 11) / (double) (1ull << 53);
}

double smoothedRandom (size_t parameterIndex, double position)
{
    const double segment = std::floor (position);
    const double fraction = position - segment;
    const double from = randomTarget (parameterIndex, (int64_t) segment);
    const double to = randomTarget (parameterIndex, (int64_t) segment + 1);
    return from + (to - from) * fraction;
}

double triangle (double position)
{
    const double phase = position - std::floor (position);
    return phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;
}

// 0..1, or nothing when the parameter does not move.
double shapeAt (const ModulatedParameter& parameter, size_t index, double seconds)
{
    const double position = seconds * (double) parameter.rateHz;
    if (parameter.rateHz == 0.0f)
        return 0.0;
    if (parameter.shape == "sine")
        return 0.5 + 0.5 * std::sin (twoPi * position);
    if (parameter.shape == "triangle")
        return triangle (position);
    if (parameter.shape == "random")
        return smoothedRandom (index, position);
    return 0.0;
}
}

DelayModulation::DelayModulation (std::vector<ModulatedParameter> p) : parameters (std::move (p)) {}

void DelayModulation::tick (double seconds, IemNode& dualDelay)
{
    for (size_t i = 0; i < parameters.size(); ++i)
    {
        const auto& parameter = parameters[i];
        const double value = (double) parameter.base + (double) parameter.depth * shapeAt (parameter, i, seconds);
        dualDelay.setParameter (parameter.parameter, (float) value);   // an unknown id is refused, not fatal
    }
}

DelayTempo::DelayTempo (IemNode& dualDelay)
    : bpmL (dualDelay.rawParameter ("delayBPML")),
      bpmR (dualDelay.rawParameter ("delayBPMR")),
      lowest (dualDelay.legalValue ("delayBPML", tempoFloor)),
      highest (dualDelay.legalValue ("delayBPML", tempoCeiling))
{
}

void DelayTempo::set (float bpm) noexcept
{
    if (! std::isfinite (bpm))
        return;
    const float clamped = std::clamp (std::clamp (bpm, tempoFloor, tempoCeiling), lowest, highest);
    bpmL->store (clamped);
    bpmR->store (clamped);
}
}
