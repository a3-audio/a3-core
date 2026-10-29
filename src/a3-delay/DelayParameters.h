// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

// The four values of the AUX-RTN delay page (BEAT, FEEDBK, COLOR, SPREAD) as DualDelay's
// parameters. Pure functions and constants, plain C++ (library a3-delay, no JUCE, no engine
// headers): the engine applies them (DelayModulation.h), the patched DualDelay reads the crossfade
// constants, and Core links a3-delay and includes "a3-delay/DelayParameters.h" to announce the
// beat that plays (fitBeatsToLine, R16).
namespace a3::delay
{
inline constexpr std::array<float, 6> legalBeats { 0.125f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f };
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

// The line and its fallback, in one place (R2, R16). DualDelay's line holds the longest time its
// legal range allows, delayBPM 46 at delayMult 1 (line sized in its prepareToPlay): 60/46 s =
// 1.304 s. A longer time halves the beats until it fits: of the legal beats only 2 below 92 BPM
// (it plays 1). Core announces this beat, the engine plays it; change the policy here only.
inline constexpr float lowestDelayBpm = 46.0f;
inline constexpr double longestDelaySeconds = 60.0 / lowestDelayBpm;
float fitBeatsToLine (float tempo, float beats);   // tempo clamped to 20..400 BPM as delayTimeFor does

// A change of the delay time, in one place (R18). The patched DualDelay
// (patches/iem/DualDelay-crossfade.patch, applied at configure time; the submodule stays at its
// pin) compares each block's target time with the one it is heading for: a change larger than
// crossfadeAboveRatio of it (a BEAT step, a tempo jump at a deck change) jumps, reading the old and
// the new position together and crossfading equal-power over crossfadeSeconds, then continues at the
// new time; a smaller one (the beat clock's drift) keeps IEM's own glide, a one-pole at
// iemDelayTimeGlideHz (DualDelay PluginProcessor.cpp prepareToPlay, `delayTimeInterp[i].setFrequency
// (0.5f, sampleRate)`, unpatched; the value here only names it).
inline constexpr float crossfadeAboveRatio = 0.02f;
inline constexpr float crossfadeSeconds = 0.020f;
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
