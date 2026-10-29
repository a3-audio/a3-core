// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include "ZoneNoise.h"

namespace
{
constexpr double rate = 44100.0;
constexpr int block = 64;
constexpr int outputs = 5;
const int burst = (int) std::lround (a3::noiseBurstSeconds * rate);
const int fade = (int) std::lround (a3::noiseFadeSeconds * rate);
const double targetRms = std::pow (10.0, a3::noiseRmsDb / 20.0);

// One zone's noise run block by block over silent buffers: its 16 decoder inputs and its outputs.
struct NoiseRun
{
    a3::ZoneNoise noise { true };
    std::vector<std::vector<float>> input = std::vector<std::vector<float>> (16), output = std::vector<std::vector<float>> (outputs);

    NoiseRun() { noise.prepare (rate, block); }

    // `at` (frame -> request) runs before the block that starts at that frame.
    void run (int frames, const std::function<void (int)>& at = {})
    {
        for (auto* set : { &input, &output })
            for (auto& channel : *set)
                channel.assign ((size_t) frames, 0.0f);
        for (int start = 0; start < frames; start += block)
        {
            if (at)
                at (start);
            const int n = std::min (block, frames - start);
            float* in[16];
            float* out[outputs];
            for (int ch = 0; ch < 16; ++ch)
                in[ch] = input[(size_t) ch].data() + start;
            for (int ch = 0; ch < outputs; ++ch)
                out[ch] = output[(size_t) ch].data() + start;
            noise.render (n);
            noise.addToInput (in, n);
            noise.addToOutputs (out, outputs, n);
        }
    }
};

double rms (const std::vector<float>& x, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (sum / std::max (1, to - from));
}

double peakOf (const std::vector<float>& x, int from, int to = -1)
{
    double worst = 0.0;
    for (int i = from; i < (to < 0 ? (int) x.size() : to); ++i)
        worst = std::max (worst, (double) std::abs (x[(size_t) i]));
    return worst;
}

// Energy in [low, high) Hz of `x` (the first 2^15 samples from `from`).
double bandEnergy (const std::vector<float>& x, int from, double low, double high)
{
    constexpr int order = 15, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (2 * size, 0.0f);
    for (int i = 0; i < size; ++i)
        data[(size_t) i] = x[(size_t) (from + i)] * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / size));
    fft.performFrequencyOnlyForwardTransform (data.data());
    double sum = 0.0;
    for (int bin = (int) (low * size / rate); bin < (int) (high * size / rate); ++bin)
        sum += (double) data[(size_t) bin] * data[(size_t) bin];
    return sum;
}
}

TEST (ZoneNoise, AChannelBurstIsOneSecondOfPinkNoiseAtItsLevel)
{
    NoiseRun r;
    r.noise.requestChannel (3);
    r.run (burst + 4410);
    const auto& played = r.output[2];
    EXPECT_NEAR (20.0 * std::log10 (rms (played, fade, burst - fade)), a3::noiseRmsDb, 1.0);
    EXPECT_EQ (peakOf (played, burst + block), 0.0);
    for (int ch : { 0, 1, 3, 4 })
        EXPECT_EQ (peakOf (r.output[(size_t) ch], 0), 0.0) << ch;
    for (const auto& channel : r.input)
        EXPECT_EQ (peakOf (channel, 0), 0.0);
}

TEST (ZoneNoise, ItIsPinkNotWhite)
{
    NoiseRun r;
    r.noise.requestChannel (1);
    r.run (burst);
    // Pink: the same energy per octave (white would have 16 times more in the higher one).
    const auto low = bandEnergy (r.output[0], fade, 200.0, 400.0), high = bandEnergy (r.output[0], fade, 3200.0, 6400.0);
    EXPECT_NEAR (10.0 * std::log10 (high / low), 0.0, 2.0);
}

TEST (ZoneNoise, ItFadesInAndOut)
{
    NoiseRun r;
    r.noise.requestChannel (2);
    r.run (burst + block);
    const auto& played = r.output[1];
    const auto steady = rms (played, burst / 2, burst / 2 + 4410);
    EXPECT_LT (std::abs (played[0]), 0.05 * steady);
    EXPECT_LT (rms (played, 0, fade / 4), 0.4 * steady);
    EXPECT_LT (rms (played, burst - fade / 4, burst), 0.4 * steady);
    EXPECT_LT (std::abs (played[(size_t) burst - 1]), 0.1 * steady);
}

TEST (ZoneNoise, ANewRequestMovesTheBurstAndTheOldOneFadesOut)
{
    NoiseRun r;
    const int moveAt = 22016;   // a block boundary, mid-burst
    r.noise.requestChannel (2);
    r.run (moveAt + burst + 2 * block, [&] (int start) { if (start == moveAt) r.noise.requestChannel (4); });
    const auto& old = r.output[1];
    const auto& moved = r.output[3];
    const auto steady = rms (old, moveAt - 4410, moveAt);
    EXPECT_GT (steady, 0.5 * targetRms);
    EXPECT_LT (rms (old, moveAt + fade - fade / 4, moveAt + fade), 0.4 * steady);   // faded, not cut
    EXPECT_GT (rms (old, moveAt, moveAt + fade / 4), 0.4 * steady);
    EXPECT_EQ (peakOf (old, moveAt + fade + block), 0.0);
    EXPECT_EQ (peakOf (moved, 0, moveAt), 0.0);
    EXPECT_LT (rms (moved, moveAt, moveAt + fade / 4), 0.4 * steady);
    EXPECT_GT (rms (moved, moveAt + burst / 2, moveAt + burst / 2 + 4410), 0.5 * targetRms);   // a whole new second
    EXPECT_EQ (peakOf (moved, moveAt + burst + block), 0.0);
}

TEST (ZoneNoise, TheSameRequestAgainRestartsIt)
{
    NoiseRun r;
    const int againAt = 30016;
    r.noise.requestChannel (2);
    r.run (againAt + burst + 4410, [&] (int start) { if (start == againAt) r.noise.requestChannel (2); });
    EXPECT_GT (rms (r.output[1], burst + 1000, burst + 5410), 0.5 * targetRms);   // past the first second: still on
    EXPECT_EQ (peakOf (r.output[1], againAt + burst + block), 0.0);
}

TEST (ZoneNoise, ADirectionGoesIntoTheDecoderInputOnly)
{
    NoiseRun r;
    r.noise.requestDirection (90.0f, 0.0f);   // left: ACN 1 (Y) carries it, ACN 3 (X) not
    r.run (burst);
    EXPECT_GT (rms (r.input[0], fade, burst - fade), 0.5 * targetRms);   // W
    EXPECT_GT (rms (r.input[1], fade, burst - fade), 0.5 * targetRms);
    EXPECT_LT (rms (r.input[3], fade, burst - fade), 1e-3 * targetRms);
    for (const auto& channel : r.output)
        EXPECT_EQ (peakOf (channel, 0), 0.0);
}

TEST (ZoneNoise, ItIsDeterministic)
{
    NoiseRun a, b;
    a.noise.requestChannel (2);
    b.noise.requestChannel (2);
    a.run (4410);
    b.run (4410);
    EXPECT_EQ (a.output[1], b.output[1]);
}

TEST (ZoneNoise, NonFiniteDirectionsAndBadChannelsAreIgnored)
{
    NoiseRun r;
    r.noise.requestDirection (std::numeric_limits<float>::quiet_NaN(), 0.0f);
    r.noise.requestChannel (0);
    r.run (4410);
    for (const auto* set : { &r.input, &r.output })
        for (const auto& channel : *set)
            EXPECT_EQ (peakOf (channel, 0), 0.0);
}
