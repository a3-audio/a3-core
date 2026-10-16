// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later AND MIT
// Algorithm: Airwindows PurestGain by Chris Johnson (MIT, 2018)
#include "PurestGain.h"
#include <algorithm>
#include <cmath>

namespace a3
{
void PurestGain::prepare (double)
{
    started = false;
    speed = 350.0;
    lastDb = unsetDb;
    framesIntoBlock = 0;
}

void PurestGain::setParameters (float a, float slowFade)
{
    pendingA = a;
    pendingB = slowFade;
}

void PurestGain::latchControlBlock()
{
    const double targetDb = double (pendingA) * 80.0 - 40.0;
    target = std::pow (10.0, targetDb / 20.0);
    latchedB = pendingB;
    if (! started)
    {
        gain = target;
        gainB = latchedB;
        started = true;
    }
    if (targetDb != lastDb)
    {
        speed *= 2.0;
        lastDb = targetDb;
    }
    speed = std::min (speed, 2500.0);
}

void PurestGain::process (float* const* io, int numSamples)
{
    int done = 0;
    while (done < numSamples)
    {
        if (framesIntoBlock == 0)
            latchControlBlock();

        const int count = std::min (numSamples - done, controlBlock - framesIntoBlock);
        for (int i = done; i < done + count; ++i)
        {
            speed = std::max (350.0, speed * 0.9999 - 0.01);
            gain = (gain * speed + target) / (speed + 1.0);
            gainB = (gainB * 4000.0 + latchedB) / 4001.0;
            const double g = gain * gainB;
            io[0][i] = float (io[0][i] * g);
            io[1][i] = float (io[1][i] * g);
        }
        done += count;
        framesIntoBlock = (framesIntoBlock + count) % controlBlock;
    }
}
}
