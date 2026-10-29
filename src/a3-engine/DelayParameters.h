// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

// The four values of the AUX-RTN delay page (BEAT, FEEDBK, COLOR, SPREAD) as DualDelay's
// parameters. Pure functions: no node, no thread; DelayModulation.h applies them.
namespace a3::delay
{
inline constexpr std::array<float, 7> legalBeats { 0.125f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f, 4.0f };
inline constexpr float defaultBeat = 0.25f;       // R1: delayMult 4 at the tempo, today's sound
inline constexpr float defaultFeedback = 0.70f;   // R10: today's f + x = 0.679
inline constexpr float defaultColor = 0.0f;
inline constexpr float defaultSpread = 60.0f;     // R9
inline constexpr float maxFeedback = 0.95f;

// Incoming values onto their legal sets: a beat to the nearest legal one in log (<= 0: the
// shortest), the others clamped (feedback 0..0.95, color -1..1, spread 0..180 degrees).
float snapBeat (float beats);
float clampFeedback (float share);
float clampColor (float color);
float clampSpread (float degrees);

// R2's single place (maintainer decision pending). DualDelay's line holds the longest time its
// legal range allows, delayBPM 46 at delayMult 1 (IEM compiled unchanged, line sized in its
// prepareToPlay): 60/46 s = 1.304 s. fitBeatsToLine is the fallback for a longer time: halve the
// beats until it fits (4 -> 2 -> 1 ...). Change the policy here and nowhere else.
inline constexpr float lowestDelayBpm = 46.0f;
inline constexpr double longestDelaySeconds = 60.0 / lowestDelayBpm;
float fitBeatsToLine (float tempo, float beats);   // tempo clamped to 20..400 BPM as delayTimeFor does

// The BEAT change glide's single anchor. a3 has no code for it: DualDelay glides its delay time
// with a one-pole at this frequency, set only in third_party/IEMPluginSuite/DualDelay/Source/
// PluginProcessor.cpp prepareToPlay, `delayTimeInterp[i].setFrequency (0.5f, sampleRate)`. A
// change of BEAT or tempo is therefore a ~1 s tape-like pitch sweep (time constant
// 1 / (2 pi 0.5 Hz) = 0.32 s), never a click. Changing it means an IEM patch or an own jump.
inline constexpr float iemDelayTimeGlideHz = 0.5f;

// BEAT: delayBPM x delayMult = tempo / fitted beats, delayMult the smallest of 1..16 that keeps
// delayBPM <= 320; `beats` is what then plays (fitBeatsToLine).
struct DelayTime
{
    float bpm;
    int mult;
    float beats;
};
DelayTime delayTimeFor (float tempo, float beats);

// FEEDBK: the share is the loop gain f + x, split in today's ratio 0.501 : 0.178 (R3), so the
// cross feedback scales with it and 0.95 cannot run away.
struct FeedbackGains
{
    float direct, cross;   // linear
};
FeedbackGains feedbackGains (float share);
float feedbackDb (float gain);   // DualDelay's dB, floored at -60 (its "off")

// COLOR (R4): below 0 the high-pass rises from 233 Hz to 2500 Hz at -1, above 0 the low-pass
// falls from 8000 Hz to 500 Hz at +1, both exponential in the value; 0 is 233 / 8000.
struct Cutoffs
{
    float highPass, lowPass;   // Hz
};
Cutoffs cutoffsFor (float color);

// One control tick's glide: each cutoff moves at most a third of an octave, the spread at most
// 10 degrees; a target within reach is reached exactly.
Cutoffs glideCutoffs (Cutoffs current, Cutoffs target);
float glideSpread (float current, float target);
}
