// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's side of the AUDIO page's block, with a runner that records instead of running.
#include <gtest/gtest.h>
#include "TimingControl.h"

namespace
{
using a3app::AudioTiming;
using a3app::Command;

struct FakeRunner final : a3app::CommandRunner
{
    Result run (const Command& command) override
    {
        ran.push_back (command);
        return { exitCode, {} };
    }
    bool start (const Command& command) override
    {
        started.push_back (command);
        return true;
    }
    std::optional<int> finished() override { return std::nullopt; }
    std::vector<Command> ran, started;
    int exitCode = 0;
};

struct Rig
{
    a3app::TimingControl control()
    {
        return a3app::TimingControl ({ runner, [this] (const AudioTiming& t) { stored.push_back (t); return storeResult; } });
    }
    FakeRunner runner;
    std::vector<AudioTiming> stored;
    juce::Result storeResult = juce::Result::ok();
};
}

// At the start the engine sets the block it remembers; the shipped 128 releases any force (nothing changes).
TEST (TimingControl, TheStartSetsTheStoredBlock)
{
    Rig rig;
    auto control = rig.control();
    control.start ({ 64 });
    ASSERT_EQ (rig.runner.ran.size(), 1u);
    EXPECT_EQ (rig.runner.ran[0], a3app::forceQuantumCommand (64));
    EXPECT_EQ (control.report().applied, (AudioTiming { 64 }));
    EXPECT_FALSE (control.report().previous.has_value());
    EXPECT_TRUE (rig.runner.started.empty()) << "nothing is restarted, ever: there are no periods";
}

// A new block runs live: one pw-metadata, the file stores it, the old one is kept for Revert. Nothing restarts.
TEST (TimingControl, ABlockAppliesLiveAndKeepsThePreviousForRevert)
{
    Rig rig;
    auto control = rig.control();
    control.start ({ 128 });
    rig.runner.ran.clear();
    EXPECT_TRUE (control.apply ({ 256 }).wasOk());
    EXPECT_EQ (rig.runner.ran, (std::vector<Command> { a3app::forceQuantumCommand (256) }));
    EXPECT_TRUE (rig.runner.started.empty());
    EXPECT_EQ (rig.stored, (std::vector<AudioTiming> { { 256 } }));
    EXPECT_EQ (control.report().applied, (AudioTiming { 256 }));
    ASSERT_TRUE (control.report().previous.has_value());
    EXPECT_EQ (*control.report().previous, (AudioTiming { 128 }));

    EXPECT_TRUE (control.apply ({ 256 }).wasOk()) << "the same again changes nothing";
    EXPECT_EQ (rig.runner.ran.size(), 1u);
}

TEST (TimingControl, ARefusedBlockChangesNothing)
{
    Rig rig;
    auto control = rig.control();
    control.start ({ 128 });
    rig.runner.exitCode = 1;
    const auto result = control.apply ({ 64 });
    EXPECT_TRUE (result.failed());
    EXPECT_TRUE (result.getErrorMessage().contains ("pw-metadata")) << result.getErrorMessage();
    EXPECT_TRUE (rig.stored.empty());
    EXPECT_EQ (control.report().applied, (AudioTiming { 128 }));
    EXPECT_FALSE (control.report().previous.has_value());
    EXPECT_TRUE (control.report().message.contains ("pw-metadata")) << control.report().message;
}

