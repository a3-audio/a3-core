// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DelayParameters.h"
#include <algorithm>
#include <cmath>

namespace a3::delay
{
namespace
{
// DualDelay's ranges above the line's floor (IEM compiled unchanged): delayBPM up to 320, delayMult 1..16.
constexpr float highestBpm = 320.0f;
constexpr int highestMult = 16;
constexpr float tempoFloor = 20.0f, tempoCeiling = 400.0f;   // delay.bpm's range
constexpr float shortestHalving = legalBeats.front() / 64.0f;  // a guard; the tempo floor never needs it

constexpr float directShare = 0.738f;
constexpr float offDb = -60.0f;

constexpr float centreHighPass = 233.0f, highestHighPass = 2500.0f;
constexpr float centreLowPass = 8000.0f, lowestLowPass = 500.0f;

constexpr float glideOctaves = 1.0f / 3.0f, glideDegrees = 10.0f;

float glideLog (float current, float target)
{
    const float octaves = std::log2 (target / current);
    if (std::abs (octaves) <= glideOctaves)
        return target;
    return current * std::exp2 (std::copysign (glideOctaves, octaves));
}
}

float snapBeat (float beats)
{
    if (! (beats > 0.0f))
        return legalBeats.front();
    const auto distance = [beats] (float legal) { return std::abs (std::log (beats / legal)); };
    return *std::min_element (legalBeats.begin(), legalBeats.end(),
                              [&] (float a, float b) { return distance (a) < distance (b); });
}

float clampFeedback (float share) { return std::clamp (share, 0.0f, maxFeedback); }
float clampColor (float color) { return std::clamp (color, -1.0f, 1.0f); }
float clampSpread (float degrees) { return std::clamp (degrees, 0.0f, 180.0f); }

float fitBeatsToLine (float tempo, float beats)
{
    tempo = std::clamp (tempo, tempoFloor, tempoCeiling);
    if (! (beats > 0.0f))
        beats = legalBeats.front();
    while (tempo / beats < lowestDelayBpm && beats > shortestHalving)
        beats *= 0.5f;
    return beats;
}

DelayTime delayTimeFor (float tempo, float beats)
{
    tempo = std::clamp (tempo, tempoFloor, tempoCeiling);
    beats = fitBeatsToLine (tempo, beats);
    int mult = 1;
    while (mult < highestMult && tempo / (beats * (float) mult) > highestBpm)
        ++mult;
    return { tempo / (beats * (float) mult), mult, beats };
}

FeedbackGains feedbackGains (float share)
{
    const float p = clampFeedback (share);
    return { p * directShare, p * (1.0f - directShare) };
}

float feedbackDb (float gain)
{
    if (! (gain > 0.0f))
        return offDb;
    return std::max (offDb, 20.0f * std::log10 (gain));
}

Cutoffs cutoffsFor (float color)
{
    const float c = clampColor (color);
    if (c < 0.0f)
        return { centreHighPass * std::pow (highestHighPass / centreHighPass, -c), centreLowPass };
    return { centreHighPass, centreLowPass * std::pow (lowestLowPass / centreLowPass, c) };
}

Cutoffs glideCutoffs (Cutoffs current, Cutoffs target)
{
    return { glideLog (current.highPass, target.highPass), glideLog (current.lowPass, target.lowPass) };
}

float glideSpread (float current, float target)
{
    return current + std::clamp (target - current, -glideDegrees, glideDegrees);
}
}
