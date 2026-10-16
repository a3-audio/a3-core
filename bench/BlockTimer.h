// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace a3bench
{
// Block durations in ms. Not for the audio thread (add() grows a vector): the
// callback hands its readings over, a reader thread adds them.
class BlockTimer
{
public:
    void add (double ms) { samples.push_back (ms); }
    void clear() { samples.clear(); }

    double mean() const
    {
        if (samples.empty())
            return 0.0;
        double sum = 0.0;
        for (double s : samples)
            sum += s;
        return sum / (double) samples.size();
    }

    // Nearest rank: the smallest value that 99 % of the readings do not exceed.
    double p99() const
    {
        if (samples.empty())
            return 0.0;
        auto sorted = samples;
        std::sort (sorted.begin(), sorted.end());
        const auto rank = (size_t) std::ceil (0.99 * (double) sorted.size());
        return sorted[std::max<size_t> (rank, 1) - 1];
    }

    double worst() const { return samples.empty() ? 0.0 : *std::max_element (samples.begin(), samples.end()); }

private:
    std::vector<double> samples;
};
}
