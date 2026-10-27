// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>

namespace a3
{
// A fixed control grid counted by sample position and carried across process()
// calls of any size: `latch()` runs at every grid boundary, `run (begin, count)`
// for each stretch of frames inside one block. With calls of exactly `size`
// frames this is the per-block rule of the Airwindows originals.
struct ControlGrid
{
    static constexpr int size = 128;
    int framesIntoBlock = 0;

    void reset() { framesIntoBlock = 0; }

    template <typename Latch, typename Run>
    void advance (int numSamples, Latch&& latch, Run&& run)
    {
        int done = 0;
        while (done < numSamples)
        {
            if (framesIntoBlock == 0)
                latch();
            const int count = std::min (numSamples - done, size - framesIntoBlock);
            run (done, count);
            done += count;
            framesIntoBlock = (framesIntoBlock + count) % size;
        }
    }
};
}
