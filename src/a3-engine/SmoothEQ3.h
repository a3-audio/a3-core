// SPDX-FileCopyrightText: 2018 Chris Johnson
// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later AND MIT
// Algorithm: Airwindows SmoothEQ3 by Chris Johnson (MIT, 2018)
#pragma once
#include "Biquad.h"
#include "ControlBlock.h"

namespace a3
{
// Two crossovers (4 kHz, 200 Hz) split treble/mid/bass by subtraction, each
// band gets its gain, then the same split again with one-pole low-passes and
// the same gains. Gain law per stage: 1 + x^3, x = 2*(p - 0.5).
//
// No smoothing: the gains jump when a control block starts. The block is a
// fixed 128-frame grid counted by sample position across process() calls;
// a value set mid-block takes effect at the next boundary.
class SmoothEQ3
{
public:
    static constexpr int maxChannels = 16;   // the deck strip

    void prepare (double sampleRate);
    void setParameters (float treble, float mid, float bass);   // each 0..1, 0.5 = flat
    void process (float* const* io, int numSamples, int numChannels = 2);   // in place, any n >= 0, up to maxChannels

private:
    struct Channel
    {
        BiquadState highLp, lowLp;
        double highPole = 0, lowPole = 0;
    };

    void latchControlBlock();
    void processChannel (Channel& st, float* io, int begin, int count) const;

    ControlGrid grid;
    BiquadCoeffs high, low;
    double highPoleCoeff = 0, lowPoleCoeff = 0;
    float pendingTreble = 0.5f, pendingMid = 0.5f, pendingBass = 0.5f;
    double gainTreble = 1.0, gainMid = 1.0, gainBass = 1.0;
    Channel channels[maxChannels];
};
}
