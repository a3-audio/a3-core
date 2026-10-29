// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <limits>
#include "EngineFixture.h"

using namespace a3test;

namespace
{
constexpr size_t oneSecond = 44100;
constexpr size_t burstLength = 22050;   // IEM's NoiseBurst: 0.5 s at 44.1 kHz
constexpr double heard = 1e-3;

// A rig with no input at all: whatever leaves it is the noise.
std::unique_ptr<EngineRig> silentRig (size_t frames, bool gateOpen = true)
{
    return std::make_unique<EngineRig> (engineConfig (4, 2), frames, engineBlock, engineRate, gateOpen);
}

// The zone's port carrying decoder output `channel` (the shipped layouts: sub 1, tops 2..5).
const std::vector<float>& portOf (const std::vector<const std::vector<float>*>& zone, int channel)
{
    return *zone[(size_t) (channel - 1)];
}

std::vector<const std::vector<float>*> allBut (const std::vector<const std::vector<float>*>& zone, int channel)
{
    auto rest = zone;
    rest.erase (rest.begin() + (channel - 1));
    return rest;
}

std::vector<const std::vector<float>*> stereo (const Stereo& s) { return { &s.l, &s.r }; }
}

TEST (EngineNoise, AMainChannelBurstLandsOnThatMainOutputOnly)
{
    const auto rig = silentRig (oneSecond);
    rig->engine->playNoise (a3::SpeakerZone::main, 3);
    rig->run();

    EXPECT_GT (peak (portOf (rig->mainOutputs(), 3)), heard);
    EXPECT_EQ (peak (allBut (rig->mainOutputs(), 3)), 0.0);
    EXPECT_EQ (peak (rig->boothOutputs()), 0.0);
    EXPECT_EQ (peak (rig->recOutputs()), 0.0);
    EXPECT_EQ (peak (stereo (rig->phones)), 0.0);
}

TEST (EngineNoise, ABoothChannelBurstLandsOnThatBoothOutputOnly)
{
    const auto rig = silentRig (oneSecond);
    rig->engine->playNoise (a3::SpeakerZone::booth, 4);
    rig->run();

    EXPECT_GT (peak (portOf (rig->boothOutputs(), 4)), heard);
    EXPECT_EQ (peak (allBut (rig->boothOutputs(), 4)), 0.0);
    EXPECT_EQ (peak (rig->mainOutputs()), 0.0);
    EXPECT_EQ (peak (rig->recOutputs()), 0.0);
}

TEST (EngineNoise, TheBurstLastsHalfASecond)
{
    const auto rig = silentRig (oneSecond);
    rig->engine->playNoise (a3::SpeakerZone::main, 2);
    rig->run();
    EXPECT_GT (peak (portOf (rig->mainOutputs(), 2), 0, burstLength), heard);
    EXPECT_EQ (peak (portOf (rig->mainOutputs(), 2), burstLength + engineBlock), 0.0);
}

TEST (EngineNoise, ItPassesTheZonesLevelStage)
{
    const auto atRest = silentRig (oneSecond / 2);
    atRest->engine->playNoise (a3::SpeakerZone::main, 3);
    atRest->run();

    const auto down = silentRig (oneSecond / 2);
    down->controls.global().masterVolume = 0.0f;
    down->engine->playNoise (a3::SpeakerZone::main, 3);
    down->run();
    EXPECT_EQ (peak (down->mainOutputs()), 0.0);

    const auto boothDown = silentRig (oneSecond / 2);
    boothDown->controls.global().booth = 0.0f;
    boothDown->engine->playNoise (a3::SpeakerZone::booth, 3);
    boothDown->run();
    EXPECT_EQ (peak (boothDown->boothOutputs()), 0.0);
    EXPECT_GT (peak (atRest->mainOutputs()), heard);
}

TEST (EngineNoise, AClosedGateKeepsItSilent)
{
    const auto rig = silentRig (oneSecond, false);
    rig->controls.global().gateOpen = false;
    rig->engine->playNoise (a3::SpeakerZone::main, 3);
    rig->engine->playNoise (a3::SpeakerZone::booth, 3);
    rig->engine->playAmbisonicNoise (a3::SpeakerZone::main, 45.0f, 0.0f);
    rig->run();
    EXPECT_EQ (peak (rig->mainOutputs()), 0.0);
    EXPECT_EQ (peak (rig->boothOutputs()), 0.0);
}

TEST (EngineNoise, ARequestWhileOneRunsIsIgnored)
{
    const auto rig = silentRig (oneSecond + oneSecond / 2);
    rig->engine->playNoise (a3::SpeakerZone::main, 2);
    rig->run (0, oneSecond / 10);
    rig->engine->playNoise (a3::SpeakerZone::main, 3);   // the first still runs
    rig->run (oneSecond / 10, oneSecond);
    EXPECT_EQ (peak (portOf (rig->mainOutputs(), 3)), 0.0);

    rig->engine->playNoise (a3::SpeakerZone::main, 3);   // the first has ended
    rig->run (oneSecond, rig->frames);
    EXPECT_GT (peak (portOf (rig->mainOutputs(), 3), oneSecond), heard);
}

TEST (EngineNoise, ZonesHaveABurstEach)
{
    const auto rig = silentRig (oneSecond);
    rig->engine->playNoise (a3::SpeakerZone::main, 2);
    rig->engine->playNoise (a3::SpeakerZone::booth, 5);
    rig->run();
    EXPECT_GT (peak (portOf (rig->mainOutputs(), 2)), heard);
    EXPECT_GT (peak (portOf (rig->boothOutputs(), 5)), heard);
    EXPECT_EQ (peak (portOf (rig->mainOutputs(), 5)), 0.0);
    EXPECT_EQ (peak (portOf (rig->boothOutputs(), 2)), 0.0);
}

TEST (EngineNoise, AnAmbisonicBurstGoesThroughThatZonesDecoderOnly)
{
    for (const auto zone : { a3::SpeakerZone::main, a3::SpeakerZone::booth })
    {
        const auto rig = silentRig (oneSecond);
        rig->engine->playAmbisonicNoise (zone, 45.0f, 0.0f);   // front left: the speaker on channel 2
        rig->run();
        const auto& played = zone == a3::SpeakerZone::main ? rig->mainOutputs() : rig->boothOutputs();
        const auto& quiet = zone == a3::SpeakerZone::main ? rig->boothOutputs() : rig->mainOutputs();
        EXPECT_GT (peak (portOf (played, 2)), heard);
        EXPECT_GT (peak (portOf (played, 2)), 2.0 * peak (portOf (played, 4)));   // the opposite speaker
        EXPECT_EQ (peak (quiet), 0.0);
        EXPECT_EQ (peak (rig->recOutputs()), 0.0);
    }
}

namespace
{
bool allFinite (const std::vector<const std::vector<float>*>& channels)
{
    for (const auto* channel : channels)
        for (const float sample : *channel)
            if (! std::isfinite (sample))
                return false;
    return true;
}
}

// S4: a non-finite direction would put NaN into the zone decoder's filters for good; it is ignored.
TEST (EngineNoise, ANonFiniteDirectionIsIgnored)
{
    const auto rig = silentRig (oneSecond);
    rig->engine->playAmbisonicNoise (a3::SpeakerZone::main, std::numeric_limits<float>::quiet_NaN(), 0.0f);
    rig->engine->playAmbisonicNoise (a3::SpeakerZone::booth, 0.0f, std::numeric_limits<float>::infinity());
    rig->run (0, oneSecond / 4);
    EXPECT_TRUE (allFinite (rig->mainOutputs()));
    EXPECT_TRUE (allFinite (rig->boothOutputs()));
    EXPECT_EQ (peak (rig->mainOutputs()), 0.0);
    EXPECT_EQ (peak (rig->boothOutputs()), 0.0);

    rig->engine->playAmbisonicNoise (a3::SpeakerZone::main, 45.0f, 0.0f);
    rig->run (oneSecond / 4, rig->frames);
    EXPECT_TRUE (allFinite (rig->mainOutputs()));
    EXPECT_GT (peak (portOf (rig->mainOutputs(), 2)), heard);
}

TEST (EngineNoise, AChannelAboveTheZonesOutputsIsSilent)
{
    const auto rig = silentRig (oneSecond / 2);
    rig->engine->playNoise (a3::SpeakerZone::main, 40);
    rig->run();
    EXPECT_EQ (peak (rig->mainOutputs()), 0.0);
    EXPECT_EQ (peak (rig->boothOutputs()), 0.0);
}
