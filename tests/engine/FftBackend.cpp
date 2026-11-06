// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// JUCE's FFT without FFTW is a portable scalar fallback, and the headphone decoder's convolution ran on it:
// 58 % of the engine's instructions at a block of 64 (callgrind, 2026-11-06). These tests keep the fast backend in.
#include <gtest/gtest.h>
#include <dlfcn.h>
#include <cmath>
#include <complex>
#include <functional>
#include <utility>
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "HeapCounter.h"

TEST (FftBackend, JucesFftIsBuiltAgainstFftw)
{
    EXPECT_EQ (JUCE_DSP_USE_STATIC_FFTW, 1);
    // Linked in, not loaded on demand: a machine with only the runtime package has no libfftw3f.so to dlopen,
    // and JUCE would fall back to the slow FFT without a word.
    EXPECT_NE (dlsym (RTLD_DEFAULT, "fftwf_plan_dft_r2c_1d"), nullptr);
}

TEST (FftBackend, RealTransformRoundTripsThroughTheBackend)
{
    for (int order = 5; order <= 15; ++order)
    {
        const int size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) size * 2, 0.0f), original ((size_t) size);
        for (int i = 0; i < size; ++i)
            data[(size_t) i] = original[(size_t) i] = std::sin (0.01f * (float) i) + 0.25f * std::cos (0.37f * (float) i);

        fft.performRealOnlyForwardTransform (data.data());
        fft.performRealOnlyInverseTransform (data.data());

        for (int i = 0; i < size; ++i)
            ASSERT_NEAR (data[(size_t) i], original[(size_t) i], 1e-3f) << "order " << order << ", sample " << i;
    }
}

// The patched JUCE runs the real transforms out of place through a scratch buffer: every bin, the mirrored upper
// half included, still has to land where JUCE's in-place contract puts it.
TEST (FftBackend, RealForwardTransformMatchesTheDefinition)
{
    for (const int order : { 5, 10 })
    {
        const int size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) size * 2, 0.0f), samples ((size_t) size);
        for (int i = 0; i < size; ++i)
            data[(size_t) i] = samples[(size_t) i] = std::sin (0.05f * (float) i) + 0.5f * std::cos (0.9f * (float) i + 0.3f);

        fft.performRealOnlyForwardTransform (data.data());

        const auto* bins = reinterpret_cast<const std::complex<float>*> (data.data());
        for (int k = 0; k < size; ++k)
        {
            std::complex<double> expected;
            for (int i = 0; i < size; ++i)
                expected += (double) samples[(size_t) i] * std::polar (1.0, -2.0 * M_PI * (double) k * (double) i / (double) size);
            ASSERT_NEAR (bins[k].real(), expected.real(), 2e-3 * size) << "order " << order << ", bin " << k;
            ASSERT_NEAR (bins[k].imag(), expected.imag(), 2e-3 * size) << "order " << order << ", bin " << k;
        }
    }
}

// Real time: FFTW plans JUCE builds with ESTIMATE|UNALIGNED often pick a buffered algorithm that mallocs scratch
// space on every execute (38 allocations a block in the engine, 2026-11-06), and in place a real inverse at n = 1024
// and 16384 mallocs even with FFTW_NO_BUFFERING. cmake/patches/juce-9.0.3-fftw-realtime.patch adds the flag and runs
// the real transforms out of place; on an unpatched JUCE this test fails. The engine uses orders 7 to 11 over the
// blocks 32 to 1024 (the BinauralDecoders and their headphone-EQ convolution; 10 at a block of 512); 5 to 15 is
// everything whose scratch the patch keeps on the stack (above that it is on the heap, as in JUCE's own fallback).
TEST (FftBackend, TransformsAllocateNothing)
{
    for (int order = 5; order <= 15; ++order)
    {
        const int size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> real ((size_t) size * 2, 0.5f);
        std::vector<std::complex<float>> in ((size_t) size, { 0.5f, 0.25f }), out ((size_t) size);
        fft.performRealOnlyForwardTransform (real.data());   // the first call may set up; the engine's calls follow it
        fft.performRealOnlyInverseTransform (real.data());
        fft.perform (in.data(), out.data(), false);
        fft.perform (in.data(), out.data(), true);

        const std::pair<const char*, std::function<void()>> transforms[] {
            { "real forward", [&] { fft.performRealOnlyForwardTransform (real.data()); } },
            { "real inverse", [&] { fft.performRealOnlyInverseTransform (real.data()); } },
            { "complex forward", [&] { fft.perform (in.data(), out.data(), false); } },
            { "complex inverse", [&] { fft.perform (in.data(), out.data(), true); } } };
        for (const auto& [name, transform] : transforms)
        {
            const auto count = a3test::heapDuring ([&] { for (int i = 0; i < 4; ++i) transform(); });
            EXPECT_EQ (count.allocated, 0) << name << ", order " << order;
            EXPECT_EQ (count.released, 0) << name << ", order " << order;
        }
    }
}
