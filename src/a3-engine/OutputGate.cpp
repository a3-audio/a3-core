// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OutputGate.h"
#include <algorithm>
#include <cmath>

namespace a3
{
namespace
{
constexpr double openSeconds = 1.0;
constexpr double closeSeconds = 0.010;
}

void OutputGate::prepare (double rate)
{
    sampleRate = rate;
    setTrim (trim);
    level = 0.0;
    published.store (0.0f, std::memory_order_relaxed);
}

void OutputGate::setTrim (std::optional<float> trimDb)
{
    trim = trimDb.has_value() && std::isfinite (*trimDb) ? trimDb : std::nullopt;
    const double full = trimGain();
    openStep = full / (openSeconds * sampleRate);
    closeStep = full / (closeSeconds * sampleRate);
    level = std::min (level, full);
}

double OutputGate::trimGain() const { return trim.has_value() ? std::pow (10.0, (double) *trim / 20.0) : 0.0; }

void OutputGate::process (float* const* channels, int numChannels, int numSamples, bool open)
{
    const double target = open ? trimGain() : 0.0;
    int i = 0;
    for (; i < numSamples && level != target; ++i)
    {
        level = level < target ? std::min (target, level + openStep) : std::max (target, level - closeStep);
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= (float) level;
    }
    if (i < numSamples && level == 0.0)
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill (channels[ch] + i, channels[ch] + numSamples, 0.0f);
    else if (i < numSamples && level != 1.0)
        for (int ch = 0; ch < numChannels; ++ch)
            for (int k = i; k < numSamples; ++k)
                channels[ch][k] *= (float) level;
    published.store ((float) level, std::memory_order_relaxed);
}

float OutputGate::currentGain() const { return published.load (std::memory_order_relaxed); }

void OutputGate::startAt (float gain)
{
    level = std::isfinite (gain) ? std::clamp ((double) gain, 0.0, trimGain()) : 0.0;
    published.store ((float) level, std::memory_order_relaxed);
}
}
