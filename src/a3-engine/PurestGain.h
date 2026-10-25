// SPDX-FileCopyrightText: 2018 Chris Johnson
// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later AND MIT
// Algorithm: Airwindows PurestGain by Chris Johnson (MIT, 2018)
#pragma once
#include "ControlBlock.h"

namespace a3
{
// Gain in dB = A*80 - 40, times a straight multiplier B ("slow fade").
// The dB gain chases its target with a one-pole whose time constant grows
// (x2 per control block that brings a new value, max 2500 samples) while the
// fader moves and relaxes back to 350 samples; B chases with 4000 samples.
//
// Control rate: a fixed 128-frame grid counted by sample position and carried
// across process() calls of any size. Parameters are latched, and the speed
// step taken, at each grid boundary; a value set mid-block takes effect at the
// next boundary. With 128-frame calls this is the original's per-block rule.
class PurestGain
{
public:
    // The original starts its remembered setting at -90 dB, so its first block
    // always counts as "a new value arrived" (speed 700); kept for sample-exact match.
    static constexpr double unsetDb = -90.0;

    // The linear gain A selects (dB = A*80-40); the one place this law lives.
    static double gainForA (double a);

    void prepare (double sampleRate);
    void setParameters (float a, float slowFade);   // A: 0..1 (dB = A*80-40), B: 0..1
    void process (float* const* io, int numSamples, int numChannels = 2); // in place (one gain for all channels), any n >= 0

private:
    void latchControlBlock();

    float pendingA = 0.5f, pendingB = 1.0f;
    float latchedB = 1.0f;
    double target = 1.0, lastDb = unsetDb;
    double speed = 350.0, gain = 1.0, gainB = 1.0;
    ControlGrid grid;
    bool started = false;
};
}
