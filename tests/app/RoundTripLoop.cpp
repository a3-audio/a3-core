// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The reserved round-trip loop in engine.json's io section: its default, what it reserves, the overlap rule
// on load, the routes the I/O page offers, and the built-in links of the loop's own ports.
#include <gtest/gtest.h>
#include "AppTruth.h"
#include "ConfigEdits.h"
#include "ConfigFile.h"
#include "ConnectionPlan.h"
#include "EngineFixture.h"
#include "RoundTripLoop.h"
#include "SpatialTestData.h"

namespace
{
struct Scratch
{
    Scratch() : dir (juce::File::createTempFile ("a3-roundtrip-loop")) { dir.createDirectory(); }
    ~Scratch() { dir.deleteRecursively(); }
    juce::File write (const juce::String& io) const
    {
        const auto spatial = juce::JSON::toString (juce::var (a3test::shippedSpatialDirectory.getFullPathName()));
        auto file = dir.getChildFile ("engine.json");
        file.replaceWithText ("{\"io\":" + io + ",\"spatial\":" + spatial + "}");
        return file;
    }
    juce::File dir;
};

a3app::AppConfig load (const juce::String& io, juce::String& error)
{
    Scratch scratch;
    const auto loaded = a3app::loadAppConfig (scratch.write (io), error, a3apptest::truthDecks());
    return loaded.value_or (a3app::AppConfig {});
}

bool contains (const std::vector<std::string>& list, const std::string& value)
{
    return std::find (list.begin(), list.end(), value) != list.end();
}
}

TEST (RoundTripLoop, AFileThatNamesNoLoopGetsTheRigsSpdifPair)
{
    juce::String error;
    const auto config = load ("{}", error);
    ASSERT_TRUE (error.isEmpty()) << error;
    ASSERT_TRUE (config.io.roundTripLoop.has_value());
    EXPECT_EQ (config.io.roundTripLoop->output, 11);
    EXPECT_EQ (config.io.roundTripLoop->input, 11);
    EXPECT_FALSE (a3app::loopHolds (config.io.roundTripLoop, false, 12)) << "one channel a side (2026-10-30): 12 is free";
}

// The loop is one channel a side (2026-10-30). A file that still says "channels": 2 (the window wrote it until then)
// loads, reserves only output 11 and input 11, and a save leaves the key out.
TEST (RoundTripLoop, AnOlderFilesChannelsAreIgnored)
{
    juce::String error;
    const auto config = load ("{\"roundTripLoop\":{\"output\":11,\"input\":11,\"channels\":2}}", error);
    ASSERT_TRUE (error.isEmpty()) << error;
    ASSERT_TRUE (config.io.roundTripLoop.has_value());
    EXPECT_TRUE (a3app::loopHolds (config.io.roundTripLoop, false, 11));
    EXPECT_FALSE (a3app::loopHolds (config.io.roundTripLoop, false, 12));
    EXPECT_FALSE (a3app::loopHolds (config.io.roundTripLoop, true, 12));
    EXPECT_EQ (a3app::loopRangeText (*config.io.roundTripLoop, false), "output 11");
    EXPECT_EQ (a3app::loopRangeText (*config.io.roundTripLoop, true), "input 11");
    const auto saved = a3app::toJson (config.io)["roundTripLoop"];
    EXPECT_FALSE (saved.getDynamicObject()->hasProperty ("channels")) << juce::JSON::toString (saved);
}

TEST (RoundTripLoop, NullMeansNoLoopAndAnObjectIsTakenAsWritten)
{
    juce::String error;
    EXPECT_FALSE (load ("{\"roundTripLoop\":null}", error).io.roundTripLoop.has_value()) << error;
    const auto config = load ("{\"roundTripLoop\":{\"output\":3,\"input\":5}}", error);
    ASSERT_TRUE (config.io.roundTripLoop.has_value()) << error;
    EXPECT_EQ (config.io.roundTripLoop->output, 3);
    EXPECT_EQ (config.io.roundTripLoop->input, 5);
}

TEST (RoundTripLoop, ABrokenLoopIsRefusedNamingIt)
{
    for (const auto* io : { "{\"roundTripLoop\":{\"output\":0,\"input\":11}}", "{\"roundTripLoop\":{\"output\":11}}",
                            "{\"roundTripLoop\":{\"output\":11,\"input\":11,\"extra\":1}}", "{\"roundTripLoop\":11}" })
    {
        juce::String error;
        load (io, error);
        EXPECT_TRUE (error.contains ("roundTripLoop")) << io << ": " << error;
    }
}

TEST (RoundTripLoop, SavedAndLoadedAgain)
{
    for (const auto& loop : { std::optional<a3app::RoundTripLoop> {}, std::optional<a3app::RoundTripLoop> ({ 3, 4 }) })
    {
        a3app::IoMap map;
        map.roundTripLoop = loop;
        const auto text = juce::JSON::toString (a3app::toJson (map));
        juce::String error;
        const auto parsed = a3app::parseIoMap (juce::JSON::parse (text), { { "analog_1" }, { "main_sub" } }, error);
        ASSERT_TRUE (parsed.has_value()) << error;
        EXPECT_EQ (parsed->roundTripLoop, loop) << text;
    }
}

TEST (RoundTripLoop, NamesTheDeviceChannelOfAPeer)
{
    using a3app::deviceChannelOfPeer;
    ASSERT_TRUE (deviceChannelOfPeer ("system:playback_11"));
    EXPECT_FALSE (deviceChannelOfPeer ("system:playback_11")->capture);
    EXPECT_EQ (deviceChannelOfPeer ("system:playback_11")->channel, 11);
    EXPECT_TRUE (deviceChannelOfPeer ("system:capture_12")->capture);
    EXPECT_EQ (deviceChannelOfPeer ("system:capture_12")->channel, 12);
    // PipeWire's pro-audio names count from AUX0: AUX10 is channel 11.
    const auto pro = deviceChannelOfPeer ("alsa_output.usb-Focusrite_Scarlett_18i20_USB_P9-00.pro-output-0:playback_AUX10");
    ASSERT_TRUE (pro);
    EXPECT_FALSE (pro->capture);
    EXPECT_EQ (pro->channel, 11);
    EXPECT_EQ (deviceChannelOfPeer ("alsa_input.usb-Focusrite-00.pro-input-0:capture_AUX11")->channel, 12);
    EXPECT_FALSE (deviceChannelOfPeer ("StemDeck-A:bformat_11"));
    EXPECT_FALSE (deviceChannelOfPeer ("system:playback_x"));
    EXPECT_FALSE (deviceChannelOfPeer ("AES67 deck B:receive_AUX10"));   // not a device's capture or playback port
}

TEST (RoundTripLoop, ReservesItsChannelsOnTheirSideOnly)
{
    const std::optional<a3app::RoundTripLoop> loop (a3app::RoundTripLoop { 11, 11 });
    EXPECT_TRUE (a3app::isLoopPeer (loop, "system:playback_11"));
    EXPECT_FALSE (a3app::isLoopPeer (loop, "system:playback_12"));
    EXPECT_FALSE (a3app::isLoopPeer (loop, "system:playback_13"));
    EXPECT_FALSE (a3app::isLoopPeer (loop, "system:playback_10"));
    EXPECT_TRUE (a3app::isLoopPeer (loop, "system:capture_11"));
    EXPECT_FALSE (a3app::isLoopPeer (loop, "system:capture_12"));
    EXPECT_FALSE (a3app::isLoopPeer (std::nullopt, "system:playback_11"));
    const std::optional<a3app::RoundTripLoop> crossed (a3app::RoundTripLoop { 3, 7 });
    EXPECT_TRUE (a3app::isLoopPeer (crossed, "system:playback_3"));
    EXPECT_FALSE (a3app::isLoopPeer (crossed, "system:capture_3"));
    EXPECT_TRUE (a3app::isLoopPeer (crossed, "system:capture_7"));
    EXPECT_FALSE (a3app::loopHolds (loop, false, 12));
    EXPECT_FALSE (a3app::loopHolds (loop, true, 13));
}

// An existing config that routes onto the loop loads: those routes are dropped and listed, the rest stays.
TEST (RoundTripLoop, RoutesOntoTheLoopAreDroppedOnLoadAndListed)
{
    juce::String error;
    const auto config = load ("{\"jack\":{\"booth_sub\":[\"system:playback_11\"],\"booth_top_1\":[\"system:playback_12\"],"
                              "\"booth_top_2\":[\"system:playback_13\"],\"analog_11\":[\"system:capture_11\"],"
                              "\"analog_1\":[\"system:capture_1\",\"alsa_input.x.pro-input-0:capture_AUX10\"]},"
                              "\"deviceChannels\":{\"main_sub\":10,\"main_top_1\":2,\"analog_2\":11},"
                              "\"directLinks\":[[\"system:capture_11\",\"x:in\"],[\"system:capture_12\",\"w:in\"],[\"y:out\",\"z:in\"]]}", error);
    ASSERT_TRUE (error.isEmpty()) << error;
    EXPECT_EQ (config.io.jack.count ("booth_sub"), 0u);
    EXPECT_EQ (config.io.jack.count ("analog_11"), 0u);
    EXPECT_EQ (config.io.jack.at ("booth_top_1"), std::vector<std::string> { "system:playback_12" }) << "12 is free again";
    EXPECT_EQ (config.io.jack.at ("booth_top_2"), std::vector<std::string> { "system:playback_13" });
    EXPECT_EQ (config.io.jack.at ("analog_1"), std::vector<std::string> { "system:capture_1" });
    EXPECT_EQ (config.io.deviceChannels.count ("main_sub"), 0u);    // device output 11
    EXPECT_EQ (config.io.deviceChannels.at ("analog_2"), 11);       // device input 12, free
    EXPECT_EQ (config.io.deviceChannels.at ("main_top_1"), 2);
    EXPECT_EQ (config.io.directLinks, (std::vector<std::pair<std::string, std::string>> { { "system:capture_12", "w:in" }, { "y:out", "z:in" } }));

    const auto& dropped = config.loopDropped;
    EXPECT_EQ (dropped.size(), 5u);
    EXPECT_TRUE (contains (dropped, "booth_sub -> system:playback_11"));
    EXPECT_TRUE (contains (dropped, "analog_1 -> alsa_input.x.pro-input-0:capture_AUX10"));
    EXPECT_TRUE (contains (dropped, "main_sub -> device channel 11"));
    EXPECT_TRUE (contains (dropped, "system:capture_11 -> x:in"));
    EXPECT_TRUE (a3app::droppedLinkLines (config).contains ("unrouted booth_sub -> system:playback_11: the round-trip loop"));
}

TEST (RoundTripLoop, WithoutALoopNothingIsDropped)
{
    juce::String error;
    const auto config = load ("{\"roundTripLoop\":null,\"jack\":{\"booth_sub\":[\"system:playback_11\"]}}", error);
    ASSERT_TRUE (error.isEmpty()) << error;
    EXPECT_EQ (config.io.jack.at ("booth_sub"), std::vector<std::string> { "system:playback_11" });
    EXPECT_TRUE (config.loopDropped.empty());
}

// The dropdowns' choices: the hardware's ports or the device's channels, without the loop's.
TEST (RoundTripLoop, TheRouteChoicesLeaveTheLoopOut)
{
    a3app::AppConfig config;
    config.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    juce::StringArray playback;
    for (int channel = 1; channel <= 14; ++channel)
        playback.add ("system:playback_" + juce::String (channel));
    const auto jack = a3app::routeChoices (config, playback, 0, false, "Scarlett 18i20 USB");
    ASSERT_EQ (jack.size(), 13u);
    EXPECT_EQ (jack[9].peer, "system:playback_10");
    EXPECT_EQ (jack[10].peer, "system:playback_12") << "12 is free again: the loop is output 11 only";
    EXPECT_EQ (jack[11].label, "Scarlett 18i20 USB out 13") << "the device's port as the user means it, not system:*";

    config.backend.type = a3app::BackendSettings::Type::device;
    const auto device = a3app::routeChoices (config, {}, 14, true, "Scarlett 18i20 USB");
    ASSERT_EQ (device.size(), 13u);
    EXPECT_EQ (device[10].channel, std::optional<int> (11));   // 0-based: device channel 12
    EXPECT_EQ (device[10].label, "channel 12");

    config.io.roundTripLoop.reset();
    EXPECT_EQ (a3app::routeChoices (config, {}, 14, true, {}).size(), 14u);
}

// A peer's name on the I/O page: a device channel by the device ("Scarlett 18i20 USB in 3"), anything else as it is.
TEST (RoundTripLoop, APeerIsNamedByTheDevice)
{
    EXPECT_EQ (a3app::peerLabel ("system:capture_3", "Scarlett 18i20 USB"), "Scarlett 18i20 USB in 3");
    EXPECT_EQ (a3app::peerLabel ("system:playback_12", "Scarlett 18i20 USB"), "Scarlett 18i20 USB out 12");
    EXPECT_EQ (a3app::peerLabel ("system:playback_1", ""), "Device out 1");
    EXPECT_EQ (a3app::peerLabel ("AES67 deck B:receive_MAIN1", "Scarlett 18i20 USB"), "AES67 deck B:receive_MAIN1");
}

TEST (RoundTripLoop, TheIoPageNoticeNamesTheLoopAndWhatWasUnrouted)
{
    a3app::AppConfig config;
    EXPECT_TRUE (a3app::loopNotice (config, {}).isEmpty());
    config.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    const auto plain = a3app::loopNotice (config, {});
    EXPECT_TRUE (plain.contains ("output 11 ")) << plain;
    EXPECT_TRUE (plain.contains ("input 11 ")) << plain;
    EXPECT_FALSE (plain.contains ("11-12")) << plain;
    EXPECT_TRUE (plain.containsIgnoreCase ("round-trip")) << plain;
    const auto dropped = a3app::loopNotice (config, { "booth_sub -> system:playback_11", "analog_11 -> system:capture_11" });
    EXPECT_TRUE (dropped.contains ("booth_sub -> system:playback_11")) << dropped;
    EXPECT_TRUE (dropped.contains ("analog_11 -> system:capture_11")) << dropped;
    config.io.roundTripLoop = a3app::RoundTripLoop { 3, 5 };
    EXPECT_TRUE (a3app::loopNotice (config, {}).contains ("output 3 ")) << a3app::loopNotice (config, {});
}

TEST (RoundTripLoop, TheEditorRefusesALoopRoute)
{
    a3app::AppConfig config;
    config.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    EXPECT_TRUE (a3app::loopRefusal (config, { "system:playback_1", "system:playback_11" }).contains ("system:playback_11"));
    EXPECT_TRUE (a3app::loopRefusal (config, { "system:playback_12" }).isEmpty()) << "12 is free again";
    EXPECT_TRUE (a3app::loopRefusal (config, { "system:playback_1" }).isEmpty());
    EXPECT_TRUE (a3app::loopRefusal (config, false, 10).contains ("11"));   // 0-based 10: device output 11
    EXPECT_TRUE (a3app::loopRefusal (config, true, 12).isEmpty());
}

// The loop's own ports link to the loop at link time, like the rec bus's sender: never written to the file.
TEST (RoundTripLoop, TheLoopsOwnPortsAreLinkedToItsChannels)
{
    a3app::AppConfig config;
    config.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    const auto map = a3app::linkMapFor (config, "");
    EXPECT_EQ (map.jack.at (a3app::loopOutputPort), std::vector<std::string> { "system:playback_11" });
    EXPECT_EQ (map.jack.at (a3app::loopInputPort), std::vector<std::string> { "system:capture_11" });
    EXPECT_EQ (config.io.jack.count (a3app::loopOutputPort), 0u);
    config.io.roundTripLoop.reset();
    EXPECT_EQ (a3app::linkMapFor (config, "").jack.count (a3app::loopOutputPort), 0u);
}

// The I/O page chooses the digital loop (2026-10-30): a channel per side, 0 is no loop. A first choice sets both
// sides (an S/PDIF or ADAT loop is out N -> in N).
TEST (RoundTripLoop, TheIoPageChoosesTheDigitalLoop)
{
    a3app::AppConfig config;
    config.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    juce::String error;
    const auto moved = a3app::withDigitalLoopChannel (config, false, 13, error);
    ASSERT_TRUE (moved) << error;
    EXPECT_EQ (moved->io.roundTripLoop, std::optional<a3app::RoundTripLoop> (a3app::RoundTripLoop { 13, 11 }));

    const auto none = a3app::withDigitalLoopChannel (config, true, 0, error);
    ASSERT_TRUE (none) << error;
    EXPECT_FALSE (none->io.roundTripLoop.has_value());

    const auto again = a3app::withDigitalLoopChannel (*none, true, 7, error);
    ASSERT_TRUE (again) << error;
    EXPECT_EQ (again->io.roundTripLoop, std::optional<a3app::RoundTripLoop> (a3app::RoundTripLoop { 7, 7 }));
}

// The reservation stays as built: the loop's channels carry no route and not the analog test pair, so a channel
// in use is refused (naming who uses it), never taken from them.
TEST (RoundTripLoop, TheDigitalLoopIsRefusedOnChannelsInUse)
{
    a3app::AppConfig config;
    config.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    config.io.jack["main_top_4"] = { "system:playback_5" };
    config.io.analogLoop = a3app::AnalogLoop { 8, 3, {} };
    juce::String error;
    EXPECT_FALSE (a3app::withDigitalLoopChannel (config, false, 5, error));   // 5 is main_top_4's
    EXPECT_TRUE (error.contains ("output 5") && error.contains ("main_top_4")) << error;
    EXPECT_FALSE (a3app::withDigitalLoopChannel (config, false, 8, error));   // 8 is the analog test output
    EXPECT_TRUE (error.contains ("output 8") && error.contains ("analog test")) << error;
    EXPECT_TRUE (a3app::withDigitalLoopChannel (config, false, 4, error)) << error << ": one channel, 5 beside it is not taken";
    EXPECT_TRUE (a3app::withDigitalLoopChannel (config, true, 2, error)) << error << ": input 2 is free, 3 beside it is not taken";
    EXPECT_FALSE (a3app::withDigitalLoopChannel (config, true, 3, error)) << "input 3 is the analog test input";

    juce::StringArray playback;
    for (int channel = 1; channel <= 14; ++channel)
        playback.add ("system:playback_" + juce::String (channel));
    const auto offered = a3app::digitalLoopChannels (config, playback, 0, false);
    EXPECT_EQ (offered, (std::vector<int> { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 }))
        << "every channel the device has is offered; a taken one is refused with its reason";
    config.backend.type = a3app::BackendSettings::Type::device;
    EXPECT_EQ (a3app::digitalLoopChannels (config, {}, 4, true), (std::vector<int> { 1, 2, 3, 4 }));
}
