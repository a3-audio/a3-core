// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Meters.h"
#include <algorithm>
#include <cmath>

namespace a3
{
void Meters::prepare (double)
{
    for (auto& s : sides)
    {
        s.sumSquares = s.windowPeak = s.rms = 0.0;
        s.peakOut.store (0.0f, std::memory_order_relaxed);
        s.rmsOut.store (0.0f, std::memory_order_relaxed);
    }
    framesInWindow = 0;
}

void Meters::push (const float* const* io, int n)
{
    int done = 0;
    while (done < n)
    {
        const int count = std::min (n - done, window - framesInWindow);
        for (int ch = 0; ch < 2; ++ch)
        {
            Side& s = sides[ch];
            for (int i = done; i < done + count; ++i)
            {
                const double x = io[ch][i];
                s.sumSquares += x * x;
                s.windowPeak = std::max (s.windowPeak, std::abs (x));
            }
        }
        done += count;
        framesInWindow += count;
        if (framesInWindow == window)
            closeWindow();
    }
}

void Meters::closeWindow()
{
    for (auto& s : sides)
    {
        const double windowRms = std::sqrt (s.sumSquares / window);
        const double coeff = windowRms > s.rms ? rmsAttack : rmsRelease;
        s.rms = coeff * windowRms + (1.0 - coeff) * s.rms;
        s.peakOut.store ((float) s.windowPeak, std::memory_order_relaxed);
        s.rmsOut.store ((float) s.rms, std::memory_order_relaxed);
        s.sumSquares = 0.0;
        s.windowPeak = 0.0;
    }
    framesInWindow = 0;
}

float Meters::peak (int ch) const { return sides[ch].peakOut.load (std::memory_order_relaxed); }
float Meters::rms (int ch) const { return sides[ch].rmsOut.load (std::memory_order_relaxed); }
}
