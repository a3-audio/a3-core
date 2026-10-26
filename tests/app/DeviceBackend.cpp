// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "DeviceBackend.h"

namespace
{
using a3app::deviceChannelFor;

a3app::PortNames ports()
{
    return { { "analog_1", "analog_2", "deckA_1" }, { "main_top_1", "main_top_2", "rec_L" } };
}
}

// No test opens a device: the types are only constructed, never scanned.
TEST (DeviceBackend, NoJuceJackDeviceType)
{
    juce::AudioDeviceManager manager;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    manager.createAudioDeviceTypes (types);
    for (auto* type : types)
        EXPECT_FALSE (type->getTypeName().containsIgnoreCase ("JACK")) << type->getTypeName();
    bool hasAlsa = false;
    for (auto* type : types)
        hasAlsa = hasAlsa || type->getTypeName() == "ALSA";
    EXPECT_TRUE (hasAlsa);
}

TEST (DeviceBackend, ChannelMapFollowsTheIoMap)
{
    a3app::IoMap io;
    io.deviceChannels = { { "analog_1", 2 }, { "analog_2", 3 }, { "main_top_1", 0 }, { "rec_L", 5 } };
    EXPECT_EQ (deviceChannelFor (io, ports(), true), (std::vector<int> { 2, 3, -1 }));
    EXPECT_EQ (deviceChannelFor (io, ports(), false), (std::vector<int> { 0, -1, 5 }));
}

TEST (DeviceBackend, UnmappedPortsAreSilentNotShifted)
{
    a3app::IoMap io;
    io.deviceChannels = { { "analog_1", 0 }, { "deckA_1", 1 }, { "main_top_2", 1 } };
    const auto in = deviceChannelFor (io, ports(), true);
    const auto out = deviceChannelFor (io, ports(), false);
    ASSERT_EQ (in.size(), 3u);
    EXPECT_EQ (in[1], -1);   // analog_2 stays unmapped; deckA_1 keeps channel 1, it does not move up to 1 by position
    EXPECT_EQ (in[2], 1);

    // The block: unmapped, out-of-range and unused channels are silence (nullptr / cleared).
    const float deviceIn0[4] { 1, 1, 1, 1 }, deviceIn1[4] { 2, 2, 2, 2 };
    const float* deviceIn[2] { deviceIn0, deviceIn1 };
    float deviceOut0[4] { 9, 9, 9, 9 }, deviceOut1[4] { 9, 9, 9, 9 }, deviceOut2[4] { 9, 9, 9, 9 };
    float* deviceOut[3] { deviceOut0, deviceOut1, deviceOut2 };

    const float* inSlots[3] { reinterpret_cast<const float*> (1), reinterpret_cast<const float*> (1), reinterpret_cast<const float*> (1) };
    float* outSlots[3] { reinterpret_cast<float*> (1), reinterpret_cast<float*> (1), reinterpret_cast<float*> (1) };
    a3app::PortSlots slots;
    for (auto& s : inSlots)
        slots.inputs.push_back (&s);
    for (auto& s : outSlots)
        slots.outputs.push_back (&s);

    a3app::bindDeviceBuffers (in, out, deviceIn, 2, deviceOut, 3, 4, slots);
    EXPECT_EQ (inSlots[0], deviceIn0);
    EXPECT_EQ (inSlots[1], nullptr);
    EXPECT_EQ (inSlots[2], deviceIn1);
    EXPECT_EQ (outSlots[0], nullptr);   // main_top_1 unmapped
    EXPECT_EQ (outSlots[1], deviceOut1);
    EXPECT_EQ (outSlots[2], nullptr);   // rec_L unmapped
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ (deviceOut0[i], 0.0f);   // no port writes channel 0: cleared, never stale
        EXPECT_EQ (deviceOut2[i], 0.0f);
    }
}

TEST (DeviceBackend, LoadIsTheLargerOfBackendAndEngineShare)
{
    // 128 frames at 48 kHz is a 2.667 ms period: 1.333 ms of engine time is 50 %.
    EXPECT_NEAR (a3app::combinedLoadPercent (20.0f, 1.3333f, 128, 48000.0), 50.0f, 0.01f);
    EXPECT_NEAR (a3app::combinedLoadPercent (70.0f, 1.3333f, 128, 48000.0), 70.0f, 0.01f);
    // Before the first block there is no period: only the backend's own figure counts.
    EXPECT_EQ (a3app::combinedLoadPercent (12.0f, 0.0f, 0, 48000.0), 12.0f);
    EXPECT_EQ (a3app::combinedLoadPercent (12.0f, 5.0f, 128, 0.0), 12.0f);
}
