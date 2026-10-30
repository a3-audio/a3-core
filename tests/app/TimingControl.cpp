// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's side of the AUDIO page's block and periods, with a runner that records instead of running.
#include <gtest/gtest.h>
#include "TimingControl.h"

namespace
{
using a3app::AudioTiming;
using a3app::Command;

const std::string scarlettOut = "alsa_output.usb-Focusrite_Scarlett_18i20_USB_P9P0A77190B57F-00.pro-output-0";
const std::string scarlettIn = "alsa_input.usb-Focusrite_Scarlett_18i20_USB_P9P0A77190B57F-00.pro-input-0";

struct FakeRunner final : a3app::CommandRunner
{
    Result run (const Command& command) override
    {
        ran.push_back (command);
        if (command == a3app::defaultNodesCommand())
            return { 0, "update: id:0 key:'default.audio.sink' value:'{\"name\":\"" + scarlettOut + "\"}' type:'Spa:String:JSON'\n"
                        "update: id:0 key:'default.audio.source' value:'{\"name\":\"" + scarlettIn + "\"}' type:'Spa:String:JSON'\n" };
        return { exitCode, {} };
    }
    bool start (const Command& command) override
    {
        started.push_back (command);
        running = true;
        return true;
    }
    std::optional<int> finished() override
    {
        if (! running || ! done)
            return std::nullopt;
        running = false;
        return startedExit;
    }
    std::vector<Command> ran, started;
    int exitCode = 0, startedExit = 0;
    bool running = false, done = false;
};

struct Rig
{
    Rig() : dir (juce::File::createTempFile ("a3-timing-control"))
    {
        dir.createDirectory();
        io.jack["main_sub"] = { "system:playback_1" };
        io.jack["analog_1"] = { "system:capture_1" };
    }
    ~Rig() { dir.deleteRecursively(); }
    a3app::TimingControl control()
    {
        return a3app::TimingControl ({ runner, dropIn(), [this] (const AudioTiming& t) { stored.push_back (t); return storeResult; },
                                       [this] { ++measured; } });
    }
    juce::File dropIn() const { return dir.getChildFile ("wireplumber.conf.d/80-a3-engine-alsa.conf"); }
    juce::File dir;
    FakeRunner runner;
    a3app::IoMap io;
    std::vector<AudioTiming> stored;
    juce::Result storeResult = juce::Result::ok();
    int measured = 0;
};
}

// At the start the engine sets the block it remembers; the shipped 128 releases any force (nothing changes).
TEST (TimingControl, TheStartSetsTheStoredBlock)
{
    Rig rig;
    auto control = rig.control();
    control.start ({ 64, 3 });
    ASSERT_EQ (rig.runner.ran.size(), 1u);
    EXPECT_EQ (rig.runner.ran[0], a3app::forceQuantumCommand (64));
    EXPECT_EQ (control.report().applied, (AudioTiming { 64, 3 }));
    EXPECT_FALSE (control.report().previous.has_value());
    EXPECT_TRUE (rig.runner.started.empty()) << "the periods are never applied at a start: WirePlumber read the file at its own";
    EXPECT_FALSE (rig.dropIn().exists());
}

// A new block runs live: one pw-metadata, the file stores it, the old one is kept for Revert. No WirePlumber restart.
TEST (TimingControl, ABlockAppliesLiveAndKeepsThePreviousForRevert)
{
    Rig rig;
    auto control = rig.control();
    control.start ({ 128, 3 });
    rig.runner.ran.clear();
    EXPECT_TRUE (control.apply ({ 256, 3 }, rig.io).wasOk());
    EXPECT_EQ (rig.runner.ran, (std::vector<Command> { a3app::forceQuantumCommand (256) }));
    EXPECT_TRUE (rig.runner.started.empty());
    EXPECT_EQ (rig.stored, (std::vector<AudioTiming> { { 256, 3 } }));
    EXPECT_EQ (control.report().applied, (AudioTiming { 256, 3 }));
    ASSERT_TRUE (control.report().previous.has_value());
    EXPECT_EQ (*control.report().previous, (AudioTiming { 128, 3 }));

    EXPECT_TRUE (control.apply ({ 256, 3 }, rig.io).wasOk()) << "the same again changes nothing";
    EXPECT_EQ (rig.runner.ran.size(), 1u);
}

TEST (TimingControl, ARefusedBlockChangesNothing)
{
    Rig rig;
    auto control = rig.control();
    control.start ({ 128, 3 });
    rig.runner.exitCode = 1;
    const auto result = control.apply ({ 64, 3 }, rig.io);
    EXPECT_TRUE (result.failed());
    EXPECT_TRUE (result.getErrorMessage().contains ("pw-metadata")) << result.getErrorMessage();
    EXPECT_TRUE (rig.stored.empty());
    EXPECT_EQ (control.report().applied, (AudioTiming { 128, 3 }));
    EXPECT_FALSE (control.report().previous.has_value());
    EXPECT_TRUE (control.report().message.contains ("pw-metadata")) << control.report().message;
}

// New periods: the drop-in for the engine's own device, WirePlumber restarted (not waited for), then a measurement
// once the device is back and linked.
TEST (TimingControl, PeriodsWriteTheDropInRestartWirePlumberAndMeasureAfterwards)
{
    Rig rig;
    auto control = rig.control();
    control.start ({ 128, 3 });
    rig.runner.ran.clear();
    EXPECT_TRUE (control.apply ({ 128, 2 }, rig.io).wasOk());
    EXPECT_EQ (rig.runner.ran, (std::vector<Command> { a3app::defaultNodesCommand() }));
    EXPECT_EQ (rig.runner.started, (std::vector<Command> { a3app::restartWirePlumberCommand() }));
    EXPECT_EQ (rig.dropIn().loadFileAsString(), a3app::periodsDropInText ({ scarlettIn, scarlettOut }, 2));
    EXPECT_TRUE (control.report().restarting);
    EXPECT_EQ (rig.stored, (std::vector<AudioTiming> { { 128, 2 } }));

    EXPECT_TRUE (control.apply ({ 64, 2 }, rig.io).failed()) << "one change at a time while WirePlumber restarts";

    control.service (10.0);
    EXPECT_EQ (rig.measured, 0);
    rig.runner.done = true;
    control.service (10.5);   // WirePlumber is back: the device's ports return, the engine links them again
    EXPECT_FALSE (control.report().restarting);
    EXPECT_EQ (rig.measured, 0) << "not before the links are made";
    control.service (10.5 + a3app::TimingControl::measureAfterSeconds);
    EXPECT_EQ (rig.measured, 1);
    control.service (20.0);
    EXPECT_EQ (rig.measured, 1);

    EXPECT_TRUE (control.apply ({ 128, 3 }, rig.io).wasOk());   // back to the default: the file goes
    EXPECT_FALSE (rig.dropIn().exists());
    EXPECT_EQ (rig.runner.started.size(), 2u);
}

TEST (TimingControl, NoDeviceInTheIoMapNoDropIn)
{
    Rig rig;
    auto control = rig.control();
    control.start ({ 128, 3 });
    a3app::IoMap decksOnly;
    decksOnly.jack["deckA_1"] = { "StemDeck-A:bformat_1" };
    EXPECT_TRUE (control.apply ({ 128, 2 }, decksOnly).failed());
    EXPECT_FALSE (rig.dropIn().exists());
    EXPECT_TRUE (rig.runner.started.empty());
    EXPECT_TRUE (rig.stored.empty());
}

TEST (TimingControl, AFailedRestartIsSaid)
{
    Rig rig;
    auto control = rig.control();
    control.start ({ 128, 3 });
    ASSERT_TRUE (control.apply ({ 128, 2 }, rig.io).wasOk());
    rig.runner.startedExit = 5;
    rig.runner.done = true;
    control.service (1.0);
    EXPECT_FALSE (control.report().restarting);
    EXPECT_TRUE (control.report().message.contains ("wireplumber")) << control.report().message;
}
