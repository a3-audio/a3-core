// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// a3-engine-cpu-bench: the whole Engine with a given deck count, offline (no JACK), every input fed,
// block times as a share of the block period. Prints one RESULT line.
#include <chrono>
#include <cstdio>
#include <thread>
#include <juce_audio_basics/juce_audio_basics.h>
#include "BlockTimer.h"
#include "CpuBench.h"
#include "OscControl.h"
#include "SpatialPorts.h"

namespace
{
constexpr int warmUpBlocks = 500;

void usage()
{
    std::fprintf (stderr, "usage: a3-engine-cpu-bench [--decks N] [--channels N] [--frames N] [--rate HZ] [--blocks N]\n"
                          "                           [--paced] [--truth FILE] [--spatial DIR]\n"
                          "  --decks N: deck slots; default the truth's deck range (the engine's default).\n"
                          "  --paced: sleep to each block period, as the rig's callback does.\n");
}

std::vector<double> timeBlocks (a3bench::CpuBenchRig& rig, const a3bench::CpuBenchOptions& o)
{
    using Clock = std::chrono::steady_clock;
    const auto period = std::chrono::duration_cast<Clock::duration> (std::chrono::duration<double> (o.frames / o.rate));
    std::vector<double> millis;
    millis.reserve ((size_t) o.blocks);
    juce::ScopedNoDenormals noDenormals;
    auto next = Clock::now();
    for (int b = -warmUpBlocks; b < o.blocks; ++b)
    {
        if (o.paced)
            std::this_thread::sleep_until (next += period);
        const auto started = Clock::now();
        rig.process (b + warmUpBlocks);
        if (b >= 0)
            millis.push_back (std::chrono::duration<double, std::milli> (Clock::now() - started).count());
    }
    return millis;
}
}

int main (int argc, char** argv)
{
    auto options = a3bench::parseCpuBenchArgs (std::vector<std::string> (argv + 1, argv + argc));
    if (! options)
        return usage(), 2;

    const auto truthText = juce::File (options->truth).loadFileAsString();
    const auto truth = a3bench::parseTruth (truthText);
    if (! truth)
        return std::fprintf (stderr, "cannot use the truth at %s\n", options->truth.c_str()), 1;
    const bool decksFromTruth = options->decks == 0;
    if (decksFromTruth)
        options->decks = a3bench::deckCountOfTruth (truthText);
    if (options->decks < 1)
        return std::fprintf (stderr, "no --decks given and the truth has no deck range\n"), 1;

    const char* home = std::getenv ("HOME");
    const auto spatialDirectory = a3bench::chooseSpatialDirectory (options->spatial, home ? home : "",
        [] (const std::string& path) { return juce::File (path).isDirectory(); });
    juce::String error;
    const auto spatial = a3::loadSpatialConfig (juce::File (spatialDirectory), error);
    if (! spatial)
        return std::fprintf (stderr, "cannot use the spatial data in %s: %s\n", spatialDirectory.c_str(), error.toRawUTF8()), 1;

    std::vector<std::string> meters;
    for (const auto& name : truth->vuMeters)
        meters.push_back (name.toStdString());
    const auto rig = a3bench::CpuBenchRig::create (*options, *spatial, meters, error);
    if (rig == nullptr)
        return std::fprintf (stderr, "cannot build the engine: %s\n", error.toRawUTF8()), 1;

    const double periodMs = 1000.0 * options->frames / options->rate;
    std::printf ("a3-engine-cpu-bench: decks %d%s, channels %d, %.0f Hz, %d frames (period %.4f ms), %d blocks, %s\n",
                 options->decks, decksFromTruth ? " (the truth's)" : "", options->channels, options->rate, options->frames,
                 periodMs, options->blocks, options->paced ? "paced" : "back to back");
    a3bench::BlockTimer timer;
    for (const double ms : timeBlocks (*rig, *options))
        timer.add (ms);
    const auto share = [&] (double ms) { return a3bench::blockSharePercent (ms, options->frames, options->rate); };
    std::printf ("block ms: mean %.4f  p99 %.4f  worst %.4f\n", timer.mean(), timer.p99(), timer.worst());
    std::printf ("RESULT decks=%d mean_ms=%.4f p99_ms=%.4f worst_ms=%.4f mean_pct=%.2f p99_pct=%.2f worst_pct=%.2f\n",
                 options->decks, timer.mean(), timer.p99(), timer.worst(), share (timer.mean()), share (timer.p99()),
                 share (timer.worst()));
    return 0;
}
