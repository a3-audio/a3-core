// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later AND MIT
// Algorithm: Airwindows Isolator3 by Chris Johnson (MIT, 2018)
#pragma once
#include "Biquad.h"
#include "ControlBlock.h"

namespace a3
{
// Band = LP14(x - LP14(x)): a 14th-order Butterworth low-pass (7 biquads) at
// the lower edge, subtracted from the input, then a second one at the upper
// edge. A soft clipper (ClipOnly2) with one sample of latency sits at the output.
//
// The coefficients glide linearly across each control block from the previous
// block's values, as in the original, including the first block, which glides
// up from all-zero coefficients (the start-up burst at high Iso). The block is
// a fixed 128-frame grid counted by sample position across process() calls; a
// value set mid-block takes effect at the next boundary.
class Isolator3
{
public:
    static constexpr int latencySamples = 1;   // ClipOnly2, at every sample rate

    void prepare (double sampleRate);
    void setParameters (float iso, float q);   // each 0..1 (the desk: 0.05..0.9)
    void process (float* const* io, int numSamples);   // stereo, in place, any n >= 0

private:
    static constexpr int sections = 7;

    struct Glide
    {
        BiquadCoeffs from[sections], to[sections];
    };

    // Direct port of Airwindows ClipOnly2 (MIT, Chris Johnson). Latency is one
    // frame at every rate: the shift loop copies intermediate[spacing] all the
    // way down to [0] in one call, so the buffer collapses and `spacing` only
    // selects which slot is written.
    struct ClipOnly2
    {
        double lastSample = 0;
        double intermediate[17] {};
        bool wasPosClip = false, wasNegClip = false;
        double process (double x, int spacing);
    };

    struct Channel
    {
        BiquadState lower[sections], upper[sections];
        ClipOnly2 clip;
    };

    void latchControlBlock();
    double edgeToNormalisedFreq (double edge) const;
    double processSample (Channel& ch, double x, const BiquadCoeffs* lower, const BiquadCoeffs* upper) const;

    ControlGrid grid;
    double overallScale = 1.0;
    int spacing = 1;
    float pendingIso = 0.5f, pendingQ = 0.0f;
    Glide lower, upper;
    Channel channels[2];
};
}
