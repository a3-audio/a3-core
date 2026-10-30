// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The AUDIO page's block and periods on PipeWire: what the engine would run and write (never PipeWire itself).
#include <gtest/gtest.h>
#include "AudioTiming.h"

namespace
{
using Command = a3app::Command;

const std::string scarlettOut = "alsa_output.usb-Focusrite_Scarlett_18i20_USB_P9P0A77190B57F-00.pro-output-0";
const std::string scarlettIn = "alsa_input.usb-Focusrite_Scarlett_18i20_USB_P9P0A77190B57F-00.pro-input-0";

// pw-metadata -n default 0, as the rig answers it (2026-10-30).
const std::string defaultMetadata =
    "Found \"default\" metadata 45\n"
    "update: id:0 key:'default.configured.audio.sink' value:'{\"name\":\"" + scarlettOut + "\"}' type:'Spa:String:JSON'\n"
    "update: id:0 key:'default.configured.audio.source' value:'{\"name\":\"" + scarlettIn + "\"}' type:'Spa:String:JSON'\n"
    "update: id:0 key:'default.audio.sink' value:'{\"name\":\"" + scarlettOut + "\"}' type:'Spa:String:JSON'\n"
    "update: id:0 key:'default.audio.source' value:'{\"name\":\"" + scarlettIn + "\"}' type:'Spa:String:JSON'\n";

struct Scratch
{
    Scratch() : dir (juce::File::createTempFile ("a3-timing")) { dir.createDirectory(); }
    ~Scratch() { dir.deleteRecursively(); }
    juce::File dir;
};
}

TEST (AudioTiming, TheChoicesAndTheDefaults)
{
    EXPECT_EQ (a3app::pipewireBlocks(), (std::vector<int> { 32, 64, 128, 256, 512, 1024 }));
    EXPECT_EQ (a3app::pipewirePeriods(), (std::vector<int> { 2, 3 }));
    const a3app::AudioTiming timing;
    EXPECT_EQ (timing.block, 128);
    EXPECT_EQ (timing.periods, 3);
    a3app::BackendSettings backend;
    backend.blockSize = 64;
    backend.periods = 2;
    EXPECT_EQ (a3app::timingOf (backend), (a3app::AudioTiming { 64, 2 }));
}

// The block goes to PipeWire's settings metadata; the shipped 128 releases the force, so PipeWire's own
// configuration (quantum 128, min-quantum 128) runs exactly as before. A forced quantum is not clamped by
// clock.min-quantum in PipeWire 1.6 (settings.check-quantum is off by default), so min-quantum is never touched.
TEST (AudioTiming, TheBlockIsTheForcedQuantum)
{
    EXPECT_EQ (a3app::forceQuantumCommand (64), (Command { "pw-metadata", "-n", "settings", "0", "clock.force-quantum", "64" }));
    EXPECT_EQ (a3app::forceQuantumCommand (1024), (Command { "pw-metadata", "-n", "settings", "0", "clock.force-quantum", "1024" }));
    EXPECT_EQ (a3app::forceQuantumCommand (128), (Command { "pw-metadata", "-n", "settings", "0", "clock.force-quantum", "0" }));
    EXPECT_EQ (a3app::defaultNodesCommand(), (Command { "pw-metadata", "-n", "default", "0" }));
    EXPECT_EQ (a3app::restartWirePlumberCommand(), (Command { "systemctl", "--user", "restart", "wireplumber" }));
}

TEST (AudioTiming, ReadsTheDefaultNodesFromPwMetadata)
{
    const auto nodes = a3app::parseDefaultNodes (defaultMetadata);
    EXPECT_EQ (nodes.sink, scarlettOut);
    EXPECT_EQ (nodes.source, scarlettIn);
    const auto none = a3app::parseDefaultNodes ("Found \"default\" metadata 45\n");
    EXPECT_TRUE (none.sink.empty());
    EXPECT_TRUE (none.source.empty());
}

// "system" is the default device (jack.default-as-system); a pro-audio port names its node itself. Anything that
// is not an ALSA device (StemDeck, the AES67 streams, beat-analyzer) is left out.
TEST (AudioTiming, TheDeviceIsTheOneTheIoMapUses)
{
    a3app::IoMap io;
    io.jack["main_sub"] = { "system:playback_1" };
    io.jack["analog_1"] = { "system:capture_1" };
    io.jack["deckA_1"] = { "StemDeck-A:bformat_1" };
    io.jack["rec_L"] = { "beat-analyzer:bpm_1", "AES67 a3nuc1 out:send_CH1" };
    const a3app::DefaultNodes defaults { scarlettOut, scarlettIn };
    EXPECT_EQ (a3app::alsaNodesOf (io, defaults), (std::vector<std::string> { scarlettIn, scarlettOut }));

    a3app::IoMap outputsOnly;
    outputsOnly.jack["main_sub"] = { "system:playback_1" };
    EXPECT_EQ (a3app::alsaNodesOf (outputsOnly, defaults), (std::vector<std::string> { scarlettOut }));
    outputsOnly.roundTripLoop = a3app::RoundTripLoop {};
    EXPECT_EQ (a3app::alsaNodesOf (outputsOnly, defaults), (std::vector<std::string> { scarlettIn, scarlettOut }))
        << "the digital loop's capture side is the default device's too";

    a3app::IoMap named;
    named.jack["main_sub"] = { "alsa_output.usb-Other_Card-00.pro-output-0:playback_AUX0" };
    named.directLinks = { { "alsa_input.usb-Other_Card-00.pro-input-0:capture_AUX3", "beat-analyzer:in_1" } };
    EXPECT_EQ (a3app::alsaNodesOf (named, defaults),
               (std::vector<std::string> { "alsa_input.usb-Other_Card-00.pro-input-0", "alsa_output.usb-Other_Card-00.pro-output-0" }));

    a3app::IoMap noDevice;
    noDevice.jack["deckA_1"] = { "StemDeck-A:bformat_1" };
    EXPECT_TRUE (a3app::alsaNodesOf (noDevice, defaults).empty());
    EXPECT_TRUE (a3app::alsaNodesOf (io, {}).empty()) << "no default device known: system names nothing";
}

TEST (AudioTiming, TheDropInIsTheEnginesOwnFile)
{
    EXPECT_EQ (a3app::periodsDropInFile (juce::File ("/home/aaa")),
               juce::File ("/home/aaa/.config/wireplumber/wireplumber.conf.d/80-a3-engine-alsa.conf"));
}

// WirePlumber 0.5's monitor.alsa.rules: one match per node (a list of matches is an "or"), the periods set on it.
TEST (AudioTiming, TheDropInSetsThePeriodsOnTheEnginesNodesOnly)
{
    const auto text = a3app::periodsDropInText ({ scarlettIn, scarlettOut }, 2);
    EXPECT_TRUE (text.contains ("monitor.alsa.rules = [")) << text;
    EXPECT_TRUE (text.contains ("{ node.name = \"" + juce::String (scarlettIn) + "\" }")) << text;
    EXPECT_TRUE (text.contains ("{ node.name = \"" + juce::String (scarlettOut) + "\" }")) << text;
    EXPECT_TRUE (text.contains ("update-props = {")) << text;
    EXPECT_TRUE (text.contains ("api.alsa.period-num = 2")) << text;
    EXPECT_FALSE (text.contains ("~alsa_")) << "no pattern: only the engine's device";
    EXPECT_TRUE (text.startsWith ("#")) << "it says who wrote it";
}

TEST (AudioTiming, WritesTheDropInAndRemovesItAtTheDefault)
{
    Scratch scratch;
    const auto file = scratch.dir.getChildFile ("wireplumber.conf.d/80-a3-engine-alsa.conf");
    const auto someoneElses = scratch.dir.getChildFile ("wireplumber.conf.d/90-a3-periods-test.conf");
    ASSERT_TRUE (file.getParentDirectory().createDirectory().wasOk());
    ASSERT_TRUE (someoneElses.replaceWithText ("# by hand\n"));

    ASSERT_TRUE (a3app::writePeriodsDropIn (file, { scarlettIn, scarlettOut }, 2).wasOk());
    EXPECT_EQ (file.loadFileAsString(), a3app::periodsDropInText ({ scarlettIn, scarlettOut }, 2));
    EXPECT_EQ (file.getParentDirectory().findChildFiles (juce::File::findFiles, false).size(), 2) << "no temporary file left";

    ASSERT_TRUE (a3app::writePeriodsDropIn (file, { scarlettIn, scarlettOut }, 3).wasOk());
    EXPECT_FALSE (file.exists()) << "the default needs no file";
    EXPECT_TRUE (someoneElses.existsAsFile()) << "never another file in that directory";
    EXPECT_TRUE (a3app::writePeriodsDropIn (file, {}, 3).wasOk()) << "nothing to remove is fine";

    EXPECT_TRUE (a3app::writePeriodsDropIn (file, {}, 2).failed()) << "no device: no rule for every ALSA device";
    EXPECT_FALSE (file.exists());
}

TEST (AudioTiming, SaysBlockAndPeriods)
{
    EXPECT_EQ (a3app::timingText (64, 3), juce::String (juce::CharPointer_UTF8 ("Block 64 frames \xc3\x97 3 periods")));
    EXPECT_EQ (a3app::timingText (128, 2), juce::String (juce::CharPointer_UTF8 ("Block 128 frames \xc3\x97 2 periods")));
}
