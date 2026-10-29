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
constexpr double feedbackRampSeconds = 0.030;   // every change, whatever its size

float gainOf (std::atomic<float>* decibels) { return juce::Decibels::decibelsToGain (decibels->load(), -60.0f); }

// R15: the stems of what the engine owns, each with an L and an R parameter.
const char* const ownedStems[] { "transformMode", "yaw", "pitch", "roll", "HPcutOff", "LPcutOff",
                                 "feedback", "xfeedback", "delayBPM", "delayMult" };
// What DelayShape forces, and to what; a saved other value is named in the note.
const std::pair<const char*, float> forcedStems[] { { "transformMode", 0.0f }, { "pitch", 0.0f }, { "roll", 0.0f } };
const char* const lineSides[] { "L", "R" };
}

bool ownsDelayParameter (const juce::String& id)
{
    for (const auto* stem : ownedStems)
        for (const auto* side : lineSides)
            if (id == juce::String (stem) + side)
                return true;
    return false;
}

std::vector<ModulatedParameter> withoutOwnedParameters (std::vector<ModulatedParameter> modulation)
{
    modulation.erase (std::remove_if (modulation.begin(), modulation.end(),
                                      [] (const ModulatedParameter& m) { return ownsDelayParameter (m.parameter); }),
                      modulation.end());
    return modulation;
}

juce::String delayOverrideNote (const juce::var& fxParameters, const std::vector<ModulatedParameter>& modulation)
{
    juce::StringArray replaced, dropped;
    for (const auto& [stem, forced] : forcedStems)
        for (const auto* side : lineSides)
        {
            const juce::String id = juce::String (stem) + side;
            const auto saved = fxParameters.getProperty (id, juce::var());
            if (! saved.isVoid() && (float) (double) saved != forced)
                replaced.add (id + " " + juce::String ((double) saved, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."));
        }
    for (const auto& m : modulation)
        if (ownsDelayParameter (m.parameter))
            dropped.addIfNotAlreadyThere (m.parameter);
    if (replaced.isEmpty() && dropped.isEmpty())
        return {};
    juce::StringArray parts;
    if (! replaced.isEmpty())
        parts.add ("replaces the saved " + replaced.joinIntoString (", ") + " (fx.parameters.json)");
    if (! dropped.isEmpty())
        parts.add ("does not run engine.json's delayModulation of " + dropped.joinIntoString (", "));
    return "the delay rotates with pitch and roll 0 and plays the delay page (R15): it " + parts.joinIntoString ("; it ");
}

DelayLoop::DelayLoop (IemNode& dualDelay)
{
    for (int i = 0; i < 2; ++i)
    {
        const juce::String side (lineSides[i]);
        sides[i] = { dualDelay.rawParameter ("delayBPM" + side), dualDelay.rawParameter ("delayMult" + side),
                           dualDelay.rawParameter ("feedback" + side), dualDelay.rawParameter ("xfeedback" + side) };
    }
    share = gainOf (sides[0].feedback) + gainOf (sides[0].xfeedback);
    rampFrom = rampTo = share;
}

void DelayLoop::prepare (double rate)
{
    sampleRate = rate;
    rampLength = std::max (1.0, feedbackRampSeconds * rate);
    rampDone = rampLength;
}

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
    const float target = delay::clampFeedback (feedbackShare);
    if (target != rampTo)
    {
        rampFrom = share;
        rampTo = target;
        rampDone = 0.0;
    }
    rampDone = std::min (rampLength, rampDone + numSamples);
    share = rampFrom + (rampTo - rampFrom) * (float) (rampDone / rampLength);
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
    for (const auto& [stem, forced] : forcedStems)
        for (const auto* side : lineSides)
            dualDelay.setParameter (juce::String (stem) + side, forced);
    write (dualDelay, true);
}

// Both lines: the cutoffs (when asked) and the yaw, +spread left, -spread right.
void DelayShape::write (IemNode& dualDelay, bool cutoffsToo)
{
    if (cutoffsToo)
        for (const auto* side : lineSides)
        {
            dualDelay.setParameter (juce::String ("HPcutOff") + side, cutoffs.highPass);
            dualDelay.setParameter (juce::String ("LPcutOff") + side, cutoffs.lowPass);
        }
    dualDelay.setParameter ("yawL", spread);
    dualDelay.setParameter ("yawR", -spread);
}

void DelayShape::tick (float color, float targetSpread, IemNode& dualDelay)
{
    const auto nextCutoffs = delay::glideCutoffs (cutoffs, delay::cutoffsFor (color));
    const bool cutoffsMove = nextCutoffs.highPass != cutoffs.highPass || nextCutoffs.lowPass != cutoffs.lowPass;
    const float nextSpread = delay::glideSpread (spread, delay::clampSpread (targetSpread));
    const bool spreadMoves = nextSpread != spread;
    cutoffs = nextCutoffs;
    spread = nextSpread;
    if (cutoffsMove || spreadMoves)
        write (dualDelay, cutoffsMove);
}
}
