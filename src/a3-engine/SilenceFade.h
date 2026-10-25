// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace a3
{
// The silence half of a gain control (R18): a weight that is exactly 1 while the
// control is above its silence threshold and exactly 0 once it is below, and
// moves between the two linearly over 10 ms (R6). Sits behind the PurestGain
// of the same control, which keeps the law's shape above 0. Open and at rest it
// is bit-exact (no multiply); closed and at rest the output is true zero, so
// no filter tail or NaN gets through.
class SilenceFade
{
public:
    void prepare (double sampleRate)
    {
        step = (float) (1.0 / std::max (1.0, 0.010 * sampleRate));
        started = false;
        level = 1.0f;
        open = true;
    }

    // The first call sets the state without a ramp (a strip starts where its controls are).
    void setOpen (bool shouldBeOpen)
    {
        open = shouldBeOpen;
        if (! started)
        {
            level = open ? 1.0f : 0.0f;
            started = true;
        }
    }

    void process (float* const* io, int numSamples)
    {
        const float target = open ? 1.0f : 0.0f;
        int i = 0;
        for (; i < numSamples && level != target; ++i)
        {
            level = open ? std::min (1.0f, level + step) : std::max (0.0f, level - step);
            io[0][i] *= level;
            io[1][i] *= level;
        }
        if (level != 0.0f)
            return;   // open and settled: untouched
        for (int ch = 0; ch < 2; ++ch)
            std::fill (io[ch] + i, io[ch] + numSamples, 0.0f);
    }

private:
    float step = 1.0f;
    float level = 1.0f;
    bool open = true, started = false;
};
}
