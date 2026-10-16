// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace a3test
{
struct Stereo { std::vector<float> l, r; };

// Uniform noise, +-level; left and right are independent.
inline Stereo noise (size_t frames, float level, uint32_t seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-level, level);
    Stereo s { std::vector<float> (frames), std::vector<float> (frames) };
    for (size_t i = 0; i < frames; ++i)
    {
        s.l[i] = dist (rng);
        s.r[i] = dist (rng);
    }
    return s;
}

inline Stereo sine (size_t frames, double hz, double sampleRate, float level)
{
    const double twoPi = 6.283185307179586;
    Stereo s { std::vector<float> (frames), std::vector<float> (frames) };
    for (size_t i = 0; i < frames; ++i)
        s.l[i] = s.r[i] = level * static_cast<float> (std::sin (twoPi * hz * static_cast<double> (i) / sampleRate));
    return s;
}

// Runs `process(float* const* io, int n)` over `in` in blocks of `blockSize`,
// calling `beforeBlock(startFrame)` first; returns the processed copy.
template <typename Proc>
Stereo runInBlocks (Proc& proc, const Stereo& in, int blockSize,
                    const std::function<void (size_t)>& beforeBlock = {})
{
    Stereo out = in;
    const size_t total = out.l.size();
    for (size_t start = 0; start < total; start += static_cast<size_t> (blockSize))
    {
        if (beforeBlock)
            beforeBlock (start);
        const int n = static_cast<int> (std::min (static_cast<size_t> (blockSize), total - start));
        float* io[2] { out.l.data() + start, out.r.data() + start };
        proc.process (io, n);
    }
    return out;
}

inline double maxAbsDiff (const Stereo& a, const Stereo& b)
{
    double worst = 0.0;
    for (size_t i = 0; i < a.l.size(); ++i)
    {
        worst = std::max (worst, std::abs (static_cast<double> (a.l[i]) - b.l[i]));
        worst = std::max (worst, std::abs (static_cast<double> (a.r[i]) - b.r[i]));
    }
    return worst;
}
}
