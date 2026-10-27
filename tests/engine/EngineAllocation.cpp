// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include "EngineFixture.h"
#include "EngineStages.h"

// The C allocator is replaced for the whole test binary (glibc's documented way,
// "Replacing malloc"): every entry forwards to glibc's own and only counts while
// the calling thread has armed it. Plan 3's counter (SpatialAllocation.cpp) only
// replaces operator new, which ends here too; JUCE's HeapBlock and IEM's code
// call malloc/calloc/realloc directly and would otherwise go unseen.
extern "C"
{
void* __libc_malloc (size_t);
void* __libc_calloc (size_t, size_t);
void* __libc_realloc (void*, size_t);
void* __libc_memalign (size_t, size_t);
void __libc_free (void*);
}

namespace
{
thread_local bool cAllocatorArmed = false;
std::atomic<int> cAllocations { 0 };
std::atomic<int> cReleases { 0 };

void noteAllocation()
{
    if (cAllocatorArmed)
        cAllocations.fetch_add (1, std::memory_order_relaxed);
}
}

extern "C"
{
void* malloc (size_t size) noexcept
{
    noteAllocation();
    return __libc_malloc (size);
}

void* calloc (size_t count, size_t size) noexcept
{
    noteAllocation();
    return __libc_calloc (count, size);
}

void* realloc (void* memory, size_t size) noexcept
{
    noteAllocation();
    return __libc_realloc (memory, size);
}

void free (void* memory) noexcept
{
    if (memory != nullptr && cAllocatorArmed)
        cReleases.fetch_add (1, std::memory_order_relaxed);
    __libc_free (memory);
}

void* memalign (size_t alignment, size_t size) noexcept
{
    noteAllocation();
    return __libc_memalign (alignment, size);
}

void* aligned_alloc (size_t alignment, size_t size) noexcept
{
    noteAllocation();
    return __libc_memalign (alignment, size);
}

int posix_memalign (void** memory, size_t alignment, size_t size) noexcept
{
    noteAllocation();
    if (alignment % sizeof (void*) != 0 || (alignment & (alignment - 1)) != 0)
        return 22;   // EINVAL
    *memory = __libc_memalign (alignment, size);
    return *memory != nullptr || size == 0 ? 0 : 12;   // ENOMEM
}
}

using namespace a3test;

namespace
{
struct HeapCount
{
    int allocated = 0, released = 0;
};

// Counts what `work` allocates and frees on this thread, disarmed before any assertion.
template <typename Work>
HeapCount countHeapDuring (Work&& work)
{
    cAllocations = 0;
    cReleases = 0;
    cAllocatorArmed = true;
    work();
    cAllocatorArmed = false;
    return { cAllocations.load(), cReleases.load() };
}

// volatile: the optimiser may otherwise elide an allocation whose result is unused.
void* (*volatile callMalloc) (size_t) = std::malloc;
void* (*volatile callCalloc) (size_t, size_t) = std::calloc;
void* (*volatile callRealloc) (void*, size_t) = std::realloc;
void (*volatile callFree) (void*) = std::free;
}

// The counter itself: it must see each form, or a zero below proves nothing.
TEST (HeapCounter, SeesTheCAllocatorNewAndHeapBlock)
{
    const auto count = countHeapDuring ([]
    {
        void* a = callMalloc (16);
        void* b = callCalloc (4, 8);
        a = callRealloc (a, 4096);
        void* c = nullptr;
        if (posix_memalign (&c, 64, 256) != 0)
            c = nullptr;
        callFree (a);
        callFree (b);
        callFree (c);
        {
            juce::HeapBlock<float> block (1024, true);   // calloc
            block.malloc (2048);                         // free + malloc
        }
        int* volatile single = new int (1);              // operator new -> malloc
        delete single;
    });
    EXPECT_EQ (count.allocated, 7);
    EXPECT_EQ (count.released, 6);
}

namespace
{
// IEM's own nodes, run alone over the chunks the engine cuts `calls` into (upstream,
// never edited): SimpleDecoder copies a juce::Array on every call
// (MatrixMultiplication::processNonReplacing), and DualDelay's AmbisonicWarp and
// AmbisonicRotator resize a scratch buffer whenever the chunk size changes. This is
// what the engine may not add to.
HeapCount upstreamAllocations (const a3::SpatialConfig& spatial, int prepared, const std::vector<int>& calls, size_t frames)
{
    std::vector<std::unique_ptr<a3::IemNode>> nodes;
    for (const auto* zone : { &spatial.main, &spatial.booth })
    {
        nodes.push_back (a3::engine::createNode (a3::IemKind::simpleDecoder, 16, zone->layout.numOutputs(), "zone"));
        a3::engine::require (nodes.back()->setParameters (zone->parameters), "zone parameters");
        a3::engine::require (nodes.back()->loadDecoderConfig (zone->layout.decoderJson), "zone decoder");
    }
    nodes.push_back (a3::engine::createNode (a3::IemKind::dualDelay, 16, 16, "fx"));
    a3::engine::require (nodes.back()->setParameters (spatial.fxParameters), "fx parameters");
    std::vector<juce::AudioBuffer<float>> buffers;
    for (auto& node : nodes)
    {
        node->prepare (engineRate, prepared);
        buffers.emplace_back (std::max (16, node->processor().getTotalNumOutputChannels()), prepared);
        buffers.back().clear();
    }

    HeapCount total;
    size_t k = 0;
    for (size_t start = 0; start < frames;)
    {
        const int n = (int) std::min<size_t> ((size_t) calls[k++ % calls.size()], frames - start);
        for (int offset = 0; offset < n; offset += prepared)
        {
            const int chunk = std::min (prepared, n - offset);
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                const auto count = countHeapDuring ([&] { a3::engine::runNode (*nodes[i], buffers[i], chunk); });
                total.allocated += count.allocated;
                total.released += count.released;
            }
        }
        start += (size_t) n;
    }
    return total;
}

constexpr int quantum = 128;

// N = 4, two decks, every path running and moving: analog and deck strips, all three
// buses, the delay, the return from both kinds of source, cue/mix/aux, the stem cues,
// the filter, the gate opening and closing, source switches (analog <-> deck crossfade,
// deck <-> deck dip). The first call is the first after prepare.
HeapCount engineAllocations (EngineRig& rig, const std::vector<int>& calls)
{
    for (int pair = 1; pair <= 4; ++pair)
        rig.setAnalogPair (pair, pinkStereo (rig.frames, (uint32_t) pair));
    rig.setAnalogPair (rig.config.returnAnalogPair, pinkStereo (rig.frames, 9));
    for (size_t d = 0; d < rig.decks.size(); ++d)
    {
        for (int ch = 0; ch < deckChannels; ++ch)
            rig.decks[d].bformat[(size_t) ch] = noise (rig.frames, 0.1f, 200 + 20 * (uint32_t) d + (uint32_t) ch).l;
        rig.decks[d].cue = { noise (rig.frames, 0.1f, 300 + (uint32_t) d).l, noise (rig.frames, 0.1f, 310 + (uint32_t) d).l };
        for (int ch = 0; ch < deckChannels; ++ch)
            rig.decks[d].aux[(size_t) ch] = noise (rig.frames, 0.1f, 320 + 20 * (uint32_t) d + (uint32_t) ch).l;
    }
    auto& g = rig.controls.global();
    g.phonesMix = 0.5f;
    g.returnCue = 1.0f;
    g.filter.frequency = 0.7f;
    for (int c = 0; c < 4; ++c)
    {
        rig.controls.channel (c).auxSend = 0.6f;
        rig.controls.channel (c).cue = c % 2 == 0 ? 1.0f : 0.0f;
        rig.controls.channel (c).filterOn = 1.0f;
    }

    // Per eighth of the run: ch1..ch4 source, return source, gate open, filter kind, filter mode.
    const int plan[8][8] { { 0, 1, 2, 0, 0, 0, 0, 1 }, { 1, 0, 2, 0, 1, 1, 0, 1 }, { 2, 0, 1, 2, 2, 1, 1, 1 },
                           { 0, 2, 1, 2, 0, 1, 1, 0 }, { 1, 2, 0, 1, 1, 0, 0, 0 }, { 2, 1, 0, 1, 2, 1, 1, 1 },
                           { 0, 1, 2, 0, 0, 1, 0, 0 }, { 1, 0, 0, 2, 1, 1, 1, 1 } };
    HeapCount total;
    float fxSeen = 0.0f;
    size_t k = 0;
    for (size_t start = 0; start < rig.frames;)
    {
        const int n = (int) std::min<size_t> ((size_t) calls[k++ % calls.size()], rig.frames - start);
        const auto& step = plan[start * 8 / rig.frames];
        for (int c = 0; c < 4; ++c)
            rig.controls.channel (c).source = step[c];
        g.returnSource = step[4];
        g.gateOpen = step[5] != 0;
        g.filter.kind = step[6];
        g.filter.mode = step[7];

        const auto in = rig.inputsAt (start);
        const auto out = rig.outputsAt (start);
        const auto count = countHeapDuring ([&] { rig.engine->process (in, out, n, rig.controls); });
        total.allocated += count.allocated;
        total.released += count.released;
        fxSeen = std::max (fxSeen, rig.engine->lastFxSendPeak());
        start += (size_t) n;
    }

    // Every path did run.
    EXPECT_GT (fxSeen, 0.0f);
    EXPECT_GT (peak (rig.mainOutputs()), 1e-4);
    EXPECT_GT (peak (rig.boothOutputs()), 1e-4);
    EXPECT_GT (peak (rig.recOutputs()), 1e-4);
    EXPECT_GT (peak (std::vector<const std::vector<float>*> { &rig.phones.l, &rig.phones.r }), 1e-4);
    for (int c = 0; c < 4; ++c)
        EXPECT_GT (peak (rig.post[(size_t) c].l), 1e-4) << "channel " << c + 1;
    return total;
}
}

// Calls of any size (shorter than the quantum, odd, longer): the engine adds no allocation
// to IEM's own (see upstreamAllocations), which are accepted (controller ruling, A8).
TEST (EngineAllocation, ProcessAddsNoAllocationBeyondIemsOwn)
{
    const std::vector<int> calls { quantum, 1, 7, 333, 2048, 64, 128, 65 };
    EngineRig rig (engineConfig (4, 2), 3 * 44100, quantum, engineRate, false);
    const auto engine = engineAllocations (rig, calls);
    const auto upstream = upstreamAllocations (rig.spatial, quantum, calls, rig.frames);
    EXPECT_EQ (engine.allocated, upstream.allocated);
    EXPECT_EQ (engine.released, upstream.released);
}

// At a fixed quantum (JACK's case) the only allocation left is IEM's SimpleDecoder:
// one malloc and one free per call, per zone. This pins the upstream finding; when
// IEM drops it, this test asks for the expectation to become zero.
TEST (EngineAllocation, AtTheQuantumOnlyIemSimpleDecoderAllocates)
{
    const std::vector<int> calls { quantum };
    EngineRig rig (engineConfig (4, 2), 344 * quantum, quantum, engineRate, false);
    const auto engine = engineAllocations (rig, calls);
    EXPECT_EQ (engine.allocated, 2 * 344);
    EXPECT_EQ (engine.released, 2 * 344);
    const auto upstream = upstreamAllocations (rig.spatial, quantum, calls, rig.frames);
    EXPECT_EQ (engine.allocated, upstream.allocated);
}

// Four deck channels, each running its aux strip into the delay (only the aux buses carry
// signal, so a send seen proves the aux path ran): the engine adds nothing to IEM's own.
TEST (EngineAllocation, DeckAuxStripsDoNotAllocate)
{
    constexpr int blocks = 100;
    EngineRig rig (engineConfig (4, 4), blocks * quantum, quantum, engineRate, false);
    for (size_t d = 0; d < rig.decks.size(); ++d)
        for (int ch = 0; ch < deckChannels; ++ch)
            rig.decks[d].aux[(size_t) ch] = noise (rig.frames, 0.1f, 700 + 20 * (uint32_t) d + (uint32_t) ch).l;
    for (int c = 0; c < 4; ++c)
    {
        rig.neutralStrip (c);
        rig.controls.channel (c).source = c + 1;
        rig.controls.channel (c).auxSend = 0.8f;
        rig.controls.channel (c).filterOn = 1.0f;
    }
    rig.controls.global().filter.frequency = 0.7f;

    HeapCount total;
    float fxSeen = 0.0f;
    for (size_t start = 0; start < rig.frames; start += quantum)
    {
        const auto in = rig.inputsAt (start);
        const auto out = rig.outputsAt (start);
        const auto count = countHeapDuring ([&] { rig.engine->process (in, out, quantum, rig.controls); });
        total.allocated += count.allocated;
        total.released += count.released;
        fxSeen = std::max (fxSeen, rig.engine->lastFxSendPeak());
    }
    EXPECT_GT (fxSeen, 1e-3f);
    const auto upstream = upstreamAllocations (rig.spatial, quantum, { quantum }, rig.frames);
    EXPECT_EQ (total.allocated, upstream.allocated);
    EXPECT_EQ (total.released, upstream.released);
    std::printf ("[ recorded ] DeckAuxStripsDoNotAllocate: %d allocations, %d releases in %d blocks (IEM's own: %d)\n",
                 total.allocated, total.released, blocks, upstream.allocated);
}
