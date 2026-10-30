// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// "system:*" on PipeWire is whatever the default sink and source are right now (jack.default-as-system). The engine
// links to it only while that is the configured device (2026-10-30: during a WirePlumber restart "system" was the
// AES67 receiver "AES67 deck C" and the AES67 sender, and the engine linked them).
#include <gtest/gtest.h>
#include "SystemDevice.h"

namespace
{
using a3app::DefaultNodes;
using a3app::DevicePresence;

const std::string scarlettOut = "alsa_output.usb-Focusrite_Scarlett_18i20_USB_P9P0A77190B57F-00.pro-output-0";
const std::string scarlettIn = "alsa_input.usb-Focusrite_Scarlett_18i20_USB_P9P0A77190B57F-00.pro-input-0";

std::string entry (const std::string& key, const std::string& node)
{
    return "update: id:0 key:'" + key + "' value:'{\"name\":\"" + node + "\"}' type:'Spa:String:JSON'\n";
}

// pw-metadata -n default 0, as the rig answers it (2026-10-30).
const std::string rigMetadata = "Found \"default\" metadata 421\n" + entry ("default.configured.audio.sink", scarlettOut)
                                + entry ("default.configured.audio.source", scarlettIn) + entry ("default.audio.sink", scarlettOut)
                                + entry ("default.audio.source", scarlettIn);

// The same while WirePlumber restarted without the Scarlett: the configured device stays, the defaults moved.
const std::string restartMetadata = "Found \"default\" metadata 421\n" + entry ("default.configured.audio.sink", scarlettOut)
                                    + entry ("default.configured.audio.source", scarlettIn) + entry ("default.audio.sink", "rtp-sink")
                                    + entry ("default.audio.source", "AES67 deck C");

struct FakeRunner final : a3app::CommandRunner
{
    Result run (const a3app::Command& command) override
    {
        ran.push_back (command);
        return next;
    }
    bool start (const a3app::Command&) override { return false; }
    std::optional<int> finished() override { return std::nullopt; }
    Result next;
    std::vector<a3app::Command> ran;
};

a3app::IoMap rigIo()
{
    a3app::IoMap io;
    io.jack["analog_1"] = { "system:capture_1" };
    io.jack["main_sub"] = { "system:playback_1" };
    io.jack["deckA_1"] = { "StemDeck-A:bformat_1" };
    io.jack["rec_L"] = { "AES67 a3nuc1 out:send_CH1" };
    io.directLinks = { { "system:capture_9", "x:in" } };
    return io;
}
}

TEST (SystemDevice, ReadsTheDefaultAndTheConfiguredNodes)
{
    EXPECT_EQ (a3app::defaultNodesCommand(), (a3app::Command { "pw-metadata", "-n", "default", "0" }));
    const auto nodes = a3app::parseDefaultNodes (restartMetadata);
    EXPECT_EQ (nodes.sink, "rtp-sink");
    EXPECT_EQ (nodes.source, "AES67 deck C");
    EXPECT_EQ (nodes.configuredSink, scarlettOut);
    EXPECT_EQ (nodes.configuredSource, scarlettIn);
    const auto none = a3app::parseDefaultNodes ("Found \"default\" metadata 45\n");
    EXPECT_TRUE (none.sink.empty() && none.source.empty() && none.configuredSink.empty() && none.configuredSource.empty());
}

TEST (SystemDevice, PresentOnlyWhileTheDefaultIsTheConfiguredDevice)
{
    EXPECT_EQ (a3app::devicePresenceOf (a3app::parseDefaultNodes (rigMetadata)), (DevicePresence { true, true }));
    EXPECT_EQ (a3app::devicePresenceOf (a3app::parseDefaultNodes (restartMetadata)), (DevicePresence { false, false }));
    // One side back before the other.
    EXPECT_EQ (a3app::devicePresenceOf ({ scarlettOut, "", scarlettOut, scarlettIn }), (DevicePresence { true, false }));
    // Nothing configured (a fresh machine): only an ALSA device counts, never a stream.
    EXPECT_EQ (a3app::devicePresenceOf ({ scarlettOut, scarlettIn, "", "" }), (DevicePresence { true, true }));
    EXPECT_EQ (a3app::devicePresenceOf ({ "rtp-sink", "AES67 deck C", "", "" }), (DevicePresence { false, false }));
    EXPECT_EQ (a3app::devicePresenceOf ({}), (DevicePresence { false, false }));
}

// The device absent: no link to system on that side, and the dropped peers are said (they are missing, not wrong).
TEST (SystemDevice, AnAbsentDeviceIsLinkedToNothing)
{
    std::vector<std::string> dropped;
    const auto none = a3app::withoutAbsentDevice (rigIo(), { false, false }, dropped);
    EXPECT_EQ (none.jack.count ("analog_1"), 0u);
    EXPECT_EQ (none.jack.count ("main_sub"), 0u);
    EXPECT_EQ (none.jack.at ("deckA_1"), (std::vector<std::string> { "StemDeck-A:bformat_1" }));
    EXPECT_EQ (none.jack.at ("rec_L"), (std::vector<std::string> { "AES67 a3nuc1 out:send_CH1" }));
    EXPECT_TRUE (none.directLinks.empty());
    EXPECT_EQ (dropped, (std::vector<std::string> { "system:capture_1", "system:playback_1", "system:capture_9" }));

    const auto playbackOnly = a3app::withoutAbsentDevice (rigIo(), { true, false }, dropped);
    EXPECT_EQ (playbackOnly.jack.at ("main_sub"), (std::vector<std::string> { "system:playback_1" }));
    EXPECT_EQ (playbackOnly.jack.count ("analog_1"), 0u);
    EXPECT_EQ (dropped, (std::vector<std::string> { "system:capture_1", "system:capture_9" }));

    const auto all = a3app::withoutAbsentDevice (rigIo(), { true, true }, dropped);
    EXPECT_EQ (all.jack, rigIo().jack);
    EXPECT_EQ (all.directLinks, rigIo().directLinks);
    EXPECT_TRUE (dropped.empty());
}

// Read once per link pass. Without PipeWire (pw-metadata never answered) system is the JACK server's own device, as
// on jackd; once it answered, a failed read keeps the last answer rather than unlinking the PA.
TEST (SystemDevice, TheWatchKeepsTheLastAnswer)
{
    FakeRunner runner;
    a3app::SystemDeviceWatch watch (runner);
    runner.next = { -1, {} };
    EXPECT_EQ (watch.read(), (DevicePresence { true, true })) << "no PipeWire: plain JACK";
    runner.next = { 0, restartMetadata };
    EXPECT_EQ (watch.read(), (DevicePresence { false, false }));
    runner.next = { -1, {} };
    EXPECT_EQ (watch.read(), (DevicePresence { false, false }));
    runner.next = { 0, rigMetadata };
    EXPECT_EQ (watch.read(), (DevicePresence { true, true }));
    runner.next = { 1, {} };
    EXPECT_EQ (watch.read(), (DevicePresence { true, true }));
    EXPECT_EQ (runner.ran.size(), 5u);
    EXPECT_EQ (runner.ran[0], a3app::defaultNodesCommand());
}

// The I/O page names the device's ports by the device ("Scarlett 18i20 USB out 1"), read from the ALSA node name
// the way ALSA's card name runs: the bus and vendor, the model, the serial number and the profile are left out.
TEST (SystemDevice, TheDeviceIsNamedFromItsNode)
{
    EXPECT_EQ (a3app::deviceNameOf (scarlettOut), "Scarlett 18i20 USB");
    EXPECT_EQ (a3app::deviceNameOf (scarlettIn), "Scarlett 18i20 USB");
    EXPECT_EQ (a3app::deviceNameOf ("alsa_output.pci-0000_00_1f.3.analog-stereo"), "Device");
    EXPECT_EQ (a3app::deviceNameOf ("rtp-sink"), "Device");
    EXPECT_EQ (a3app::deviceNameOf (""), "Device");
    // The configured device's name, else the current default's, else "Device".
    EXPECT_EQ (a3app::deviceNameOf (DefaultNodes { "rtp-sink", "AES67 deck C", scarlettOut, scarlettIn }), "Scarlett 18i20 USB");
    EXPECT_EQ (a3app::deviceNameOf (DefaultNodes { scarlettOut, "", "", "" }), "Scarlett 18i20 USB");
    EXPECT_EQ (a3app::deviceNameOf (DefaultNodes {}), "Device");
}
