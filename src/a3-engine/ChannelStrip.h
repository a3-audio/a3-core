// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <vector>
#include "ControlBlock.h"
#include "Isolator3.h"
#include "Meters.h"
#include "PurestGain.h"
#include "SmoothEQ3.h"

namespace a3
{
struct StripControls            // desk/Motion values 0..1, as on the wire
{
    std::atomic<float> gain { 0.0f }, volume { 0.0f }, eqHigh { 0.5f }, eqMid { 0.5f }, eqLow { 0.5f },
                       frequency { 0.5f }, q { 0.0f }, threeD { 0.0f };
};

struct StripMeters { float prePeak[2], preRms[2], postPeak[2], postRms[2]; };

// One channel of the REAPER template (n-input, n-stereo-enc, n-multi-enc,
// n-channelbus), see .claude/notes/plan-2-step-0-strip-today.md:
//
//   x      = EQ (TRIM (in))                              [pre meter]
//   moving = Isolator3 (BAND (x))                        n-stereo-enc ch 1/2
//   steady = x - moving                                  n-multi-enc bed at 0 dB + the inverted copy
//   moving, steady through VOLUME                        n-channelbus; [post meter] = moving + steady
//
// BAND is a PurestGain at bandGain(3D) *before* Isolator3, as in the template's
// gain container. The bed is not delayed for Isolator3's frame: the template
// has no delay there (Airwindows reports no latency), so moving + steady is x
// itself; whether the cut should be latency-aligned waits on the rig's
// bed-leak measurement. 3D at or below the law's floor gives BAND A 0 = -40 dB,
// not a mute: the moving pair keeps the band at -40 dB, as in REAPER, and a
// closing band glides with PurestGain instead of being cut.
//
// Controls are read at a fixed 128-frame grid counted across calls, so the
// output does not depend on the period the host picks.
class ChannelStrip
{
public:
    void prepare (double sampleRate, int maxBlock);      // allocates; the only call that may
    // in: stereo input (selected source); moving/steady: stereo outputs. Any numSamples >= 0.
    // preOut (optional): also receives x, the signal after TRIM and EQ (the pre meter's tap).
    void process (const float* const* in, float* const* moving, float* const* steady, int numSamples,
                  float* const* preOut = nullptr);
    StripControls& controls() { return state; }
    StripMeters meters();   // from another thread: RMS now, peaks since the last call (the call takes them)

private:
    void latchControls();
    void processStretch (const float* const* in, float* const* moving, float* const* steady, float* const* preOut,
                         int count);

    StripControls state;
    PurestGain trim, bandGain, volumeMoving, volumeSteady;
    SmoothEQ3 eq;
    Isolator3 isolator;
    Meters pre, post;
    ControlGrid grid;
    int maxBlock = 0;
    std::vector<float> x[2], sum[2];
};
}
