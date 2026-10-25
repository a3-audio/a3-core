// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChannelStrip.h"
#include <algorithm>
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Laws.h"

namespace a3
{
void ChannelStrip::prepare (double sampleRate, int maxBlockSize)
{
    maxBlock = std::max (1, maxBlockSize);
    for (int ch = 0; ch < 2; ++ch)
    {
        x[ch].assign ((size_t) maxBlock, 0.0f);
        sum[ch].assign ((size_t) maxBlock, 0.0f);
    }
    for (auto* gain : { &trim, &bandGain, &volumeMoving, &volumeSteady })
        gain->prepare (sampleRate);
    eq.prepare (sampleRate);
    isolator.prepare (sampleRate);
    pre.prepare (sampleRate);
    post.prepare (sampleRate);
    if (sharedFilter != nullptr)
        filter.prepare (sampleRate, maxBlock, 2);
    grid.reset();
}

void ChannelStrip::latchControls()
{
    const auto load = [] (const std::atomic<float>& a) { return (double) a.load (std::memory_order_relaxed); };
    trim.setParameters ((float) laws::slopeVolume (load (state.gain)), 1.0f);
    eq.setParameters ((float) laws::slopeEq (load (state.eqHigh)), (float) laws::slopeEq (load (state.eqMid)),
                      (float) laws::slopeEq (load (state.eqLow)));
    isolator.setParameters ((float) laws::isolatorParam (load (state.frequency)),
                            (float) laws::isolatorParam (load (state.q)));
    bandGain.setParameters ((float) laws::bandGain (load (state.threeD)), 1.0f);
    const auto volume = (float) laws::slopeVolume (load (state.volume));
    volumeMoving.setParameters (volume, 1.0f);
    volumeSteady.setParameters (volume, 1.0f);
}

void ChannelStrip::process (const float* const* in, float* const* moving, float* const* steady, int numSamples,
                           float* const* preOut)
{
    StripBuffers out { { moving[0], moving[1] }, { steady[0], steady[1] } };
    if (preOut != nullptr)
        out.pre[0] = preOut[0], out.pre[1] = preOut[1];
    process (in, out, numSamples);
}

void ChannelStrip::process (const float* const* in, const StripBuffers& out, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    if (maxBlock == 0)
        return;   // not prepared
    const auto shifted = [] (float* const* planes, int at) -> std::array<float*, 2>
    {
        return { planes[0] != nullptr ? planes[0] + at : nullptr, planes[1] != nullptr ? planes[1] + at : nullptr };
    };
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int chunk = std::min (maxBlock, numSamples - offset);
        grid.advance (chunk,
                      [this] { latchControls(); },
                      [&] (int begin, int count)
                      {
                          const int at = offset + begin;
                          const float* i[2] { in[0] + at, in[1] + at };
                          StripBuffers part;
                          const auto m = shifted (out.moving, at);
                          const auto s = shifted (out.steady, at);
                          const auto mp = shifted (out.movingPreVolume, at);
                          const auto sp = shifted (out.steadyPreVolume, at);
                          const auto p = shifted (out.pre, at);
                          std::copy (m.begin(), m.end(), part.moving);
                          std::copy (s.begin(), s.end(), part.steady);
                          std::copy (mp.begin(), mp.end(), part.movingPreVolume);
                          std::copy (sp.begin(), sp.end(), part.steadyPreVolume);
                          std::copy (p.begin(), p.end(), part.pre);
                          processStretch (i, part, count);
                      });
    }
}

// One stretch inside one grid block: every unit's own grid lines up with the
// strip's, so the parameters latched above take effect at this boundary.
void ChannelStrip::processStretch (const float* const* in, const StripBuffers& out, int count)
{
    float* xs[2] { x[0].data(), x[1].data() };
    for (int ch = 0; ch < 2; ++ch)
        std::copy (in[ch], in[ch] + count, xs[ch]);
    trim.process (xs, count);
    eq.process (xs, count);
    pre.push (xs, count);
    if (out.pre[0] != nullptr)
        for (int ch = 0; ch < 2; ++ch)
            std::copy (xs[ch], xs[ch] + count, out.pre[ch]);

    if (sharedFilter != nullptr)
        filter.process (xs, 2, count, state.filterOn.load (std::memory_order_relaxed) >= 0.5f, *sharedFilter);

    float* const* moving = out.moving;
    float* const* steady = out.steady;
    for (int ch = 0; ch < 2; ++ch)
        std::copy (xs[ch], xs[ch] + count, moving[ch]);
    bandGain.process (moving, count);
    isolator.process (moving, count);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < count; ++i)
            steady[ch][i] = xs[ch][i] - moving[ch][i];

    if (out.movingPreVolume[0] != nullptr)
        for (int ch = 0; ch < 2; ++ch)
            std::copy (moving[ch], moving[ch] + count, out.movingPreVolume[ch]);
    if (out.steadyPreVolume[0] != nullptr)
        for (int ch = 0; ch < 2; ++ch)
            std::copy (steady[ch], steady[ch] + count, out.steadyPreVolume[ch]);

    volumeMoving.process (moving, count);
    volumeSteady.process (steady, count);

    float* sums[2] { sum[0].data(), sum[1].data() };
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < count; ++i)
            sums[ch][i] = moving[ch][i] + steady[ch][i];
    post.push (sums, count);
}

StripMeters ChannelStrip::meters()
{
    StripMeters m {};
    for (int ch = 0; ch < 2; ++ch)
    {
        m.prePeak[ch] = pre.takePeak (ch);
        m.preRms[ch] = pre.rms (ch);
        m.postPeak[ch] = post.takePeak (ch);
        m.postRms[ch] = post.rms (ch);
    }
    return m;
}
}
