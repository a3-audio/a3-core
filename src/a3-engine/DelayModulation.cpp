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

namespace
{
constexpr float feedbackRampSeconds = 0.030f;   // the whole 0..0.95 range

float gainOf (std::atomic<float>* decibels) { return juce::Decibels::decibelsToGain (decibels->load(), -60.0f); }
}

DelayLoop::DelayLoop (IemNode& dualDelay)
{
    const char* names[2] = { "L", "R" };
    for (int i = 0; i < 2; ++i)
    {
        const juce::String side (names[i]);
        sides[i] = { dualDelay.rawParameter ("delayBPM" + side), dualDelay.rawParameter ("delayMult" + side),
                     dualDelay.rawParameter ("feedback" + side), dualDelay.rawParameter ("xfeedback" + side) };
    }
    share = gainOf (sides[0].feedback) + gainOf (sides[0].xfeedback);
}

void DelayLoop::prepare (double rate) { sampleRate = rate; }

void DelayLoop::set (float tempo, float beats, float feedbackShare, int numSamples) noexcept
{
    if (std::isfinite (tempo) && std::isfinite (beats))
    {
        const auto time = delay::delayTimeFor (tempo, beats);
        for (auto& side : sides)
        {
            side.bpm->store (time.bpm);
            side.mult->store ((float) time.mult);
        }
    }
    if (! std::isfinite (feedbackShare))
        return;
    const float reach = delay::maxFeedback * (float) numSamples / (feedbackRampSeconds * (float) sampleRate);
    const float target = delay::clampFeedback (feedbackShare);
    share += std::clamp (target - share, -reach, reach);
    const auto gains = delay::feedbackGains (share);
    for (auto& side : sides)
    {
        side.feedback->store (delay::feedbackDb (gains.direct));
        side.xfeedback->store (delay::feedbackDb (gains.cross));
    }
}

DelayShape::DelayShape (IemNode& dualDelay)
    : cutoffs { dualDelay.rawParameter ("HPcutOffL")->load(), dualDelay.rawParameter ("LPcutOffL")->load() },
      spread (dualDelay.rawParameter ("yawL")->load())
{
}

void DelayShape::tick (float color, float targetSpread, IemNode& dualDelay)
{
    const auto nextCutoffs = delay::glideCutoffs (cutoffs, delay::cutoffsFor (color));
    if (nextCutoffs.highPass != cutoffs.highPass)
    {
        dualDelay.setParameter ("HPcutOffL", nextCutoffs.highPass);
        dualDelay.setParameter ("HPcutOffR", nextCutoffs.highPass);
    }
    if (nextCutoffs.lowPass != cutoffs.lowPass)
    {
        dualDelay.setParameter ("LPcutOffL", nextCutoffs.lowPass);
        dualDelay.setParameter ("LPcutOffR", nextCutoffs.lowPass);
    }
    cutoffs = nextCutoffs;

    const float nextSpread = delay::glideSpread (spread, delay::clampSpread (targetSpread));
    if (nextSpread != spread)
    {
        dualDelay.setParameter ("yawL", nextSpread);
        dualDelay.setParameter ("yawR", -nextSpread);
    }
    spread = nextSpread;
}
}
