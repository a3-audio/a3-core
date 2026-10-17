// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include "SpatialBusFixture.h"

using namespace a3test;

// The guard against regressions; the worst case < 0.3 ms is the bench's job on the rig.
TEST (SpatialTiming, FourStripsAndTheBusPerSixtyFourFrames)
{
#ifndef NDEBUG
    GTEST_SKIP() << "timing is measured in Release only";
#endif
    const auto config = loadedSpatialConfig (shippedSpatialDirectory);
    const size_t inputFrames = 44096;
    const auto inputs = pinkOnAllStrips (inputFrames);

    std::array<std::unique_ptr<a3::ChannelStrip>, a3::numSpatialStrips> strips;
    for (auto& strip : strips)
        strip = stripAtTheFixedPoint();
    TapSignals taps { { silentStereo (spatialQuantum), silentStereo (spatialQuantum), silentStereo (spatialQuantum), silentStereo (spatialQuantum) },
                      { silentStereo (spatialQuantum), silentStereo (spatialQuantum), silentStereo (spatialQuantum), silentStereo (spatialQuantum) } };

    a3::SpatialBus bus (config);
    bus.prepare (spatialRate, spatialQuantum);
    a3::SpatialControls positions;
    place (positions, staticPlacement);
    OutputSignals storage (config, spatialQuantum);
    const auto out = storage.at (0);
    const auto stripTaps = taps.at (0);

    double worst = 0, total = 0;
    const int blocks = 20000;
    for (int b = 0; b < blocks; ++b)
    {
        const size_t start = ((size_t) b * spatialQuantum) % (inputFrames - spatialQuantum);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t s = 0; s < strips.size(); ++s)
        {
            const float* in[2] { inputs[s].l.data() + start, inputs[s].r.data() + start };
            float* moving[2] { taps.moving[s].l.data(), taps.moving[s].r.data() };
            float* steady[2] { taps.steady[s].l.data(), taps.steady[s].r.data() };
            strips[s]->process (in, moving, steady, spatialQuantum);
        }
        bus.process (stripTaps, out, spatialQuantum, positions);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        worst = std::max (worst, ms);
        total += ms;
    }
    const double mean = total / blocks;
    RecordProperty ("mean_ms", std::to_string (mean));
    RecordProperty ("worst_ms", std::to_string (worst));
    std::printf ("SpatialTiming: mean_ms %.6f worst_ms %.6f\n", mean, worst);
    // Measured split on a3coreV01 (i5-4440): BinauralDecoder ≈ 0.205 ms/block alone, with JUCE's
    // fallback FFT (FFTW is excluded by the spec). This is a regression guard; acceptance is
    // the rig's 10-minute worst-case run.
    EXPECT_LT (mean, 0.35);
}

