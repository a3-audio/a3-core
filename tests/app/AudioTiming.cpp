// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The AUDIO page's block on PipeWire: what the engine would run (never PipeWire itself).
#include <gtest/gtest.h>
#include "AudioTiming.h"

namespace
{
using Command = a3app::Command;

struct Scratch
{
    Scratch() : dir (juce::File::createTempFile ("a3-timing")) { dir.createDirectory(); }
    ~Scratch() { dir.deleteRecursively(); }
    juce::File dir;
};
}

TEST (AudioTiming, TheChoicesAndTheDefault)
{
    EXPECT_EQ (a3app::pipewireBlocks(), (std::vector<int> { 32, 64, 128, 256, 512, 1024 }));
    const a3app::AudioTiming timing;
    EXPECT_EQ (timing.block, 128);
    a3app::BackendSettings backend;
    backend.blockSize = 64;
    EXPECT_EQ (a3app::timingOf (backend), (a3app::AudioTiming { 64 }));
}

// The block goes to PipeWire's settings metadata; the shipped 128 releases the force, so PipeWire's own
// configuration (quantum 128, min-quantum 128) runs exactly as before. A forced quantum is not clamped by
// clock.min-quantum in PipeWire 1.6 (settings.check-quantum is off by default), so min-quantum is never touched.
TEST (AudioTiming, TheBlockIsTheForcedQuantum)
{
    EXPECT_EQ (a3app::forceQuantumCommand (64), (Command { "pw-metadata", "-n", "settings", "0", "clock.force-quantum", "64" }));
    EXPECT_EQ (a3app::forceQuantumCommand (1024), (Command { "pw-metadata", "-n", "settings", "0", "clock.force-quantum", "1024" }));
    EXPECT_EQ (a3app::forceQuantumCommand (128), (Command { "pw-metadata", "-n", "settings", "0", "clock.force-quantum", "0" }));
}

// Periods are gone (2026-10-30: 2 periods measured 348 samples, 3 periods 344; PipeWire drives the Scarlett by
// timer). The engine removes the WirePlumber drop-in an older engine wrote, that file only.
TEST (AudioTiming, TheRetiredPeriodsDropInIsRemovedAndNothingElse)
{
    Scratch scratch;
    const auto file = a3app::retiredPeriodsDropIn (scratch.dir);
    EXPECT_EQ (file, scratch.dir.getChildFile (".config/wireplumber/wireplumber.conf.d/80-a3-engine-alsa.conf"));
    const auto someoneElses = file.getSiblingFile ("90-by-hand.conf");
    ASSERT_TRUE (file.getParentDirectory().createDirectory().wasOk());
    ASSERT_TRUE (file.replaceWithText ("monitor.alsa.rules = []\n"));
    ASSERT_TRUE (someoneElses.replaceWithText ("# by hand\n"));

    EXPECT_TRUE (a3app::removeRetiredPeriodsDropIn (scratch.dir));
    EXPECT_FALSE (file.exists());
    EXPECT_TRUE (someoneElses.existsAsFile());
    EXPECT_FALSE (a3app::removeRetiredPeriodsDropIn (scratch.dir)) << "nothing to remove: nothing to say";
}

TEST (AudioTiming, SaysTheBlock)
{
    EXPECT_EQ (a3app::timingText (64), juce::String ("Block 64 frames"));
    EXPECT_EQ (a3app::timingText (128), juce::String ("Block 128 frames"));
}
