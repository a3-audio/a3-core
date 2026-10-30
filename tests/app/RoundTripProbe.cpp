// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The measured round trip: the cross-correlation on synthetic loops (known delays, noise, no loop),
// the probe as the audio thread drives it, and the AUDIO page's line.
#include <cmath>
#include <random>
#include <gtest/gtest.h>
#include "HeapCounter.h"
#include "RoundTripProbe.h"

namespace
{
using a3app::findLoopDelay;
using a3app::testSequence;

// What the loop input hears: the sent signal `delay` samples late, plus white noise at `noiseRms`.
std::vector<float> looped (const std::vector<float>& sent, int delay, int length, float noiseRms = 0.0f, float gain = 1.0f)
{
    std::vector<float> heard ((size_t) length, 0.0f);
    for (size_t i = 0; i < sent.size() && (size_t) delay + i < heard.size(); ++i)
        heard[(size_t) delay + i] = gain * sent[i];
    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, noiseRms);
    if (noiseRms > 0.0f)
        for (auto& sample : heard)
            sample += noise (rng);
    return heard;
}

int recordingLength (const std::vector<float>& sent) { return (int) sent.size() + 4096; }
}

TEST (RoundTripProbe, TheTestSignalIsQuietAndShort)
{
    const auto sent = testSequence();
    EXPECT_EQ (sent.size(), 8191u);   // order 13: 0.19 s at 44.1 kHz
    float peak = 0.0f;
    for (const auto sample : sent)
        peak = std::max (peak, std::abs (sample));
    EXPECT_NEAR (20.0 * std::log10 (peak), -20.0, 0.01);
}

TEST (RoundTripProbe, FindsKnownDelays)
{
    const auto sent = testSequence();
    for (const int delay : { 0, 1, 64, 191, 384, 1037, 4000 })
    {
        const auto found = findLoopDelay (sent, looped (sent, delay, recordingLength (sent)));
        EXPECT_TRUE (found.found) << delay;
        EXPECT_EQ (found.samples, delay);
    }
}

TEST (RoundTripProbe, FindsTheDelayUnderNoiseAndAttenuation)
{
    const auto sent = testSequence();
    const auto found = findLoopDelay (sent, looped (sent, 517, recordingLength (sent), 0.03f, 0.5f));   // about 4 dB SNR
    EXPECT_TRUE (found.found);
    EXPECT_EQ (found.samples, 517);
}

TEST (RoundTripProbe, SilenceIsNoLoop)
{
    const auto sent = testSequence();
    EXPECT_FALSE (findLoopDelay (sent, std::vector<float> ((size_t) recordingLength (sent), 0.0f)).found);
}

TEST (RoundTripProbe, NoiseAloneIsNoLoop)
{
    const auto sent = testSequence();
    EXPECT_FALSE (findLoopDelay (sent, looped (sent, 0, recordingLength (sent), 0.1f, 0.0f)).found);
}

TEST (RoundTripProbe, SomethingElseOnTheInputIsNoLoop)
{
    const auto sent = testSequence();
    std::vector<float> tone ((size_t) recordingLength (sent));
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.3f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * (double) i / 48000.0);
    EXPECT_FALSE (findLoopDelay (sent, tone).found);
}

TEST (RoundTripProbe, ALoopTooLongForTheWindowIsNoLoop)
{
    const auto sent = testSequence();
    // Only the first 300 samples of the signal come back inside the recording: not a clear peak.
    EXPECT_FALSE (findLoopDelay (sent, looped (sent, recordingLength (sent) - 300, recordingLength (sent))).found);
}

namespace
{
// Runs the probe like a backend: blocks of `block` samples, the loop `delay` samples long (a FIFO between
// what the probe wrote and what it reads), until the recording is complete.
a3app::LoopDelay runProbe (a3app::RoundTripProbe& probe, int block, int delay, bool loopPatched = true)
{
    std::vector<float> line ((size_t) delay, 0.0f);   // the loop: out now, in `delay` samples later
    std::vector<float> in ((size_t) block), out ((size_t) block);
    for (int blocks = 0; blocks < 1000 && ! probe.finished(); ++blocks)
    {
        probe.process (in.data(), out.data(), block);
        for (int i = 0; i < block; ++i)
        {
            line.push_back (out[(size_t) i]);
            in[(size_t) i] = loopPatched ? line.front() : 0.0f;
            line.erase (line.begin());
        }
    }
    const auto result = probe.collect();
    EXPECT_TRUE (result.has_value());
    return result.value_or (a3app::LoopDelay {});
}
}

TEST (RoundTripProbe, MeasuresTheLoopAsTheAudioThreadRunsIt)
{
    a3app::RoundTripProbe probe;
    ASSERT_TRUE (probe.start());
    // Read before write in each block: a loop of d samples comes back d + one block later.
    const auto found = runProbe (probe, 128, 200);
    EXPECT_TRUE (found.found);
    EXPECT_EQ (found.samples, 200 + 128);
}

TEST (RoundTripProbe, AnUnpatchedLoopIsNoLoopFound)
{
    a3app::RoundTripProbe probe;
    ASSERT_TRUE (probe.start());
    EXPECT_FALSE (runProbe (probe, 64, 100, false).found);
}

TEST (RoundTripProbe, OneMeasurementAtATime)
{
    a3app::RoundTripProbe probe;
    ASSERT_TRUE (probe.start());
    EXPECT_FALSE (probe.start());
    runProbe (probe, 256, 10);
    EXPECT_TRUE (probe.start());   // collected: free again
}

TEST (RoundTripProbe, SilentOnTheLoopWhenIdleAndNullBuffersAreSafe)
{
    a3app::RoundTripProbe probe;
    std::vector<float> out (64, 1.0f);
    probe.process (nullptr, out.data(), 64);
    for (const auto sample : out)
        EXPECT_EQ (sample, 0.0f);
    ASSERT_TRUE (probe.start());
    probe.process (nullptr, nullptr, 64);   // no loop port: records silence, writes nowhere
}

TEST (RoundTripProbe, TheAudioThreadNeverAllocates)
{
    a3app::RoundTripProbe probe;
    ASSERT_TRUE (probe.start());
    std::vector<float> in (128, 0.0f), out (128, 0.0f);
    const auto count = a3test::heapDuring ([&]
    {
        for (int i = 0; i < 400 && ! probe.finished(); ++i)
            probe.process (in.data(), out.data(), 128);
    });
    EXPECT_EQ (count.allocated, 0);
    EXPECT_EQ (count.released, 0);
    EXPECT_TRUE (probe.finished());
}
