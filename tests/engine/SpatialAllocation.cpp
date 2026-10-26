// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <atomic>
#include <cstdlib>
#include <new>
#include "AmbiEncoderBus.h"
#include "SpatialBusFixture.h"

// Every form of the global operator new/delete is replaced for the whole test
// binary, but only counts while the calling thread has armed it: other tests
// and other threads see the plain allocator.
namespace
{
thread_local bool countingArmed = false;
std::atomic<int> allocations { 0 };
std::atomic<int> deallocations { 0 };

void* allocate (std::size_t size, std::size_t alignment = 0)
{
    if (countingArmed)
        allocations.fetch_add (1, std::memory_order_relaxed);
    if (size == 0)
        size = 1;
    if (alignment <= alignof (std::max_align_t))
        return std::malloc (size);
    void* memory = nullptr;
    return posix_memalign (&memory, alignment, size) == 0 ? memory : nullptr;
}

void* allocateOrThrow (std::size_t size, std::size_t alignment = 0)
{
    if (auto* memory = allocate (size, alignment))
        return memory;
    throw std::bad_alloc();
}

void release (void* memory) noexcept
{
    if (memory == nullptr)
        return;
    if (countingArmed)
        deallocations.fetch_add (1, std::memory_order_relaxed);
    std::free (memory);
}

struct AllocationCount
{
    int allocated = 0, released = 0;
};

// Counts what `work` allocates and frees on this thread. Disarmed before the
// caller's assertions, whose failure messages allocate themselves.
template <typename Work>
AllocationCount countDuring (Work&& work)
{
    allocations = 0;
    deallocations = 0;
    countingArmed = true;
    work();
    countingArmed = false;
    return { allocations.load(), deallocations.load() };
}
}

void* operator new (std::size_t size) { return allocateOrThrow (size); }
void* operator new[] (std::size_t size) { return allocateOrThrow (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept { return allocate (size); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { return allocate (size); }
void* operator new (std::size_t size, std::align_val_t alignment) { return allocateOrThrow (size, (std::size_t) alignment); }
void* operator new[] (std::size_t size, std::align_val_t alignment) { return allocateOrThrow (size, (std::size_t) alignment); }
void* operator new (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return allocate (size, (std::size_t) alignment); }
void* operator new[] (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return allocate (size, (std::size_t) alignment); }

void operator delete (void* memory) noexcept { release (memory); }
void operator delete[] (void* memory) noexcept { release (memory); }
void operator delete (void* memory, std::size_t) noexcept { release (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { release (memory); }
void operator delete (void* memory, const std::nothrow_t&) noexcept { release (memory); }
void operator delete[] (void* memory, const std::nothrow_t&) noexcept { release (memory); }
void operator delete (void* memory, std::align_val_t) noexcept { release (memory); }
void operator delete[] (void* memory, std::align_val_t) noexcept { release (memory); }
void operator delete (void* memory, std::size_t, std::align_val_t) noexcept { release (memory); }
void operator delete[] (void* memory, std::size_t, std::align_val_t) noexcept { release (memory); }
void operator delete (void* memory, std::align_val_t, const std::nothrow_t&) noexcept { release (memory); }
void operator delete[] (void* memory, std::align_val_t, const std::nothrow_t&) noexcept { release (memory); }

using namespace a3test;

namespace
{
constexpr size_t longestCall = 4096;

struct BusRun
{
    explicit BusRun (const a3::SpatialConfig& config)
        : taps (renderStrips (pinkOnAllStrips (longestCall))), out (config, longestCall)
    {
        place (positions, staticPlacement);
    }

    TapSignals taps;
    OutputSignals out;
    a3::SpatialControls positions;
};

class SpatialAllocation : public testing::TestWithParam<bool>
{
protected:
    const juce::File& dataDirectory() const { return GetParam() ? reaperBoothSpatialDirectory : shippedSpatialDirectory; }
};
}

// The counter itself: it must see an allocation, or a zero below proves nothing.
TEST (AllocationCounter, SeesEveryForm)
{
    struct alignas (64) Wide { float lanes[16]; };
    const auto count = countDuring ([]
    {
        // volatile: the optimiser may otherwise elide a new/delete pair.
        int* volatile single = new int (1);
        delete single;
        int* volatile array = new int[3];
        delete[] array;
        int* volatile nothrow = new (std::nothrow) int (2);
        delete nothrow;
        Wide* volatile wide = new Wide();
        delete wide;
        Wide* volatile wides = new Wide[2];
        delete[] wides;
    });
    EXPECT_EQ (count.allocated, 5);
    EXPECT_EQ (count.released, 5);
}

INSTANTIATE_TEST_SUITE_P (DataDirectories, SpatialAllocation, testing::Values (false, true),
                          [] (const testing::TestParamInfo<bool>& info) { return info.param ? "reaperBooth61" : "shipped"; });

TEST_P (SpatialAllocation, ProcessAllocatesNothingForAnyCallSize)
{
    const auto config = loadedSpatialConfig (dataDirectory());
    BusRun run (config);
    a3::SpatialBus bus (config);
    bus.prepare (spatialRate, spatialQuantum);

    for (const int calls : { 1, 7, 64, 65, 300, 4096 })
    {
        const auto in = run.taps.at (0);
        const auto out = run.out.at (0);
        const auto count = countDuring ([&] { bus.process (in, out, calls, run.positions); });
        EXPECT_EQ (count.allocated, 0) << calls << " frames";
        EXPECT_EQ (count.released, 0) << calls << " frames";
    }
}

// The bench swaps in a bus it built and prepared off the audio thread; its
// very first callback must not allocate either.
TEST_P (SpatialAllocation, AFreshlyPreparedBusAllocatesNothingOnItsFirstCall)
{
    const auto config = loadedSpatialConfig (dataDirectory());
    BusRun run (config);
    juce::String error;
    auto bus = a3::SpatialBus::create (config, error);
    ASSERT_TRUE (bus != nullptr) << error;
    bus->prepare (spatialRate, spatialQuantum);

    const auto in = run.taps.at (0);
    const auto out = run.out.at (0);
    const auto count = countDuring ([&] { bus->process (in, out, spatialQuantum, run.positions); });
    EXPECT_EQ (count.allocated, 0);
    EXPECT_EQ (count.released, 0);
}

// Sixty-one analog sources: encoder 0 has 64 inputs, more than JUCE's
// preallocated channel table, so its per-chunk view must not be rebuilt.
TEST (AmbiEncoderBus, ProcessAllocatesNothingForAnyCallSize)
{
    constexpr int count = 61;
    juce::String error;
    auto bus = a3::AmbiEncoderBus::create (count, loadedSpatialConfig (shippedSpatialDirectory), error);
    ASSERT_TRUE (bus != nullptr) << error;
    bus->prepare (spatialRate, spatialQuantum);

    std::vector<Stereo> signals;
    for (int c = 0; c < count; ++c)
        signals.push_back (pinkStereo (longestCall, (uint32_t) (c + 1)));
    std::vector<a3::StereoIn> moving, steady;
    for (const auto& s : signals)
    {
        moving.push_back ({ s.l.data(), s.r.data() });
        steady.push_back ({ s.r.data(), s.l.data() });
    }
    const a3::StereoIn ret { signals[0].l.data(), signals[1].r.data() };
    std::vector<std::atomic<float>> azimuth (count), elevation (count);
    juce::AudioBuffer<float> bus16 (16, (int) longestCall);

    for (const int calls : { 64, 1, 7, 65, 300, 4096 })
    {
        const auto allocations = countDuring ([&] { bus->process (moving, steady, ret, bus16, calls, azimuth.data(), elevation.data()); });
        EXPECT_EQ (allocations.allocated, 0) << calls << " frames";
        EXPECT_EQ (allocations.released, 0) << calls << " frames";
    }
}
