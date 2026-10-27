// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include "EngineFixture.h"
#include "Laws.h"

using namespace a3test;

namespace
{
constexpr size_t oneSecond = 44100;
constexpr size_t fadeOut = 441;   // 10 ms
constexpr float calibratedDb = -30.88f;   // main and booth on the rig (spec Scope 7)

double calibratedGain() { return std::pow (10.0, calibratedDb / 20.0); }

std::vector<const std::vector<float>*> stereo (const Stereo& s) { return { &s.l, &s.r }; }

a3::EngineConfig calibratedConfig()
{
    auto config = engineConfig (4, 2);
    config.trims.main = config.trims.booth = config.trims.phones = calibratedDb;
    return config;
}

std::unique_ptr<EngineRig> closedRig (const a3::EngineConfig& config, size_t frames)
{
    return std::make_unique<EngineRig> (config, frames, engineBlock, engineRate, false);
}

// A constant on deck B's stem cue: phones L = level x cueWeight (0) x PHONES x gate,
// sample for sample (the stem cue is added after the decoder, StemCueLandsAfterTheDecoder).
constexpr float cueLevel = 0.5f;
void constantStemCue (EngineRig& rig) { rig.decks[1].cue[0] = std::vector<float> (rig.frames, cueLevel); }
double phonesUnit() { return cueLevel * a3::laws::cueWeight (0.0) * a3::laws::stageGain (0.75); }

void signalEverywhere (EngineRig& rig)
{
    rig.setAnalogPair (1, pinkStereo (rig.frames, 3));
    for (int ch = 0; ch < deckChannels; ++ch)
        rig.decks[0].bformat[(size_t) ch] = noise (rig.frames, 0.1f, 40 + (uint32_t) ch).l;
    rig.decks[1].cue[0] = noise (rig.frames, 0.2f, 60).l;
    rig.controls.channel (1).source = 1;
    rig.controls.channel (0).cue = 1.0f;
    rig.controls.channel (0).auxSend = 0.5f;
    rig.controls.global().phonesMix = 0.5f;
}
}

TEST (EngineGate, ClosedAtStart)
{
    const auto rig = closedRig (engineConfig (4, 2), oneSecond / 2);
    signalEverywhere (*rig);
    rig->controls.global().gateOpen = false;
    rig->run();

    EXPECT_EQ (peak (rig->mainOutputs()), 0.0);
    EXPECT_EQ (peak (rig->boothOutputs()), 0.0);
    EXPECT_EQ (peak (stereo (rig->phones)), 0.0);
    EXPECT_GT (peak (rig->recOutputs()), 1e-3);   // rec is never gated
    const auto levels = rig->engine->gateLevels();
    for (float gain : levels.gain)
        EXPECT_EQ (gain, 0.0f);
}

TEST (EngineGate, OpensOverOneSecondToTheTrim)
{
    const size_t frames = 2 * oneSecond;
    const auto rig = closedRig (calibratedConfig(), frames);
    constantStemCue (*rig);
    rig->setAnalogPair (1, pinkStereo (frames, 5));
    rig->controls.global().gateOpen = true;
    rig->run();

    std::vector<double> gain (frames);
    for (size_t i = 0; i < frames; ++i)
        gain[i] = rig->phones.l[i] / phonesUnit();
    size_t reached = frames;
    for (size_t i = 0; i < frames && reached == frames; ++i)
        if (gain[i] >= calibratedGain() * (1.0 - 1e-5))
            reached = i;
    EXPECT_NEAR ((double) reached, (double) oneSecond, 128.0);
    EXPECT_NEAR (gain[oneSecond / 2], calibratedGain() / 2.0, 0.01 * calibratedGain());   // linear in gain
    for (size_t i = 1; i < frames; ++i)
        ASSERT_GE (gain[i], gain[i - 1] - 1e-9) << "not monotone at " << i;
    for (size_t i = oneSecond + 128; i < frames; ++i)
        ASSERT_NEAR (gain[i], calibratedGain(), 1e-5 * calibratedGain()) << i;
    EXPECT_NEAR (rig->engine->gateLevels().gain[(int) a3::GatedZone::phones], calibratedGain(), 1e-6);

    // Main follows the same ramp: against a rig that started at the trim, equal once open.
    const auto open = std::make_unique<EngineRig> (calibratedConfig(), frames);
    open->setAnalogPair (1, pinkStereo (frames, 5));
    open->run();
    EXPECT_GT (peak (open->mainOutputs(), oneSecond), 1e-4);
    EXPECT_LT (maxDifference (rig->mainOutputs(), open->mainOutputs(), oneSecond + 256), 1e-6 * peak (open->mainOutputs()));
    const double half = energy (rig->mainOutputs(), oneSecond / 2 - 1000, oneSecond / 2 + 1000)
                        / energy (open->mainOutputs(), oneSecond / 2 - 1000, oneSecond / 2 + 1000);
    EXPECT_NEAR (half, 0.25, 0.03);
}

TEST (EngineGate, ZoneWithoutTrimStaysClosed)
{
    auto config = engineConfig (4, 2);
    config.trims.booth = std::nullopt;
    EngineRig rig (config, oneSecond);
    rig.engine->startGateAt ({ { 1.0f, 1.0f, 1.0f } });   // a hand-over cannot open it either
    rig.setAnalogPair (1, pinkStereo (oneSecond, 7));
    rig.controls.global().gateOpen = true;
    rig.run();

    EXPECT_EQ (peak (rig.boothOutputs()), 0.0);
    EXPECT_GT (peak (rig.mainOutputs()), 1e-3);
    EXPECT_EQ (rig.engine->gateLevels().gain[(int) a3::GatedZone::booth], 0.0f);
    EXPECT_EQ (rig.engine->gateLevels().gain[(int) a3::GatedZone::main], 1.0f);
}

TEST (EngineGate, RestartedEngineIsClosed)
{
    const size_t frames = oneSecond;
    EngineRig before (engineConfig (4, 2), frames);
    before.setAnalogPair (1, pinkStereo (frames, 9));
    before.run();
    ASSERT_GT (peak (before.mainOutputs()), 1e-3);

    // A new Engine starts closed and stays closed until it is told to open.
    const auto after = closedRig (engineConfig (4, 2), frames);
    after->setAnalogPair (1, pinkStereo (frames, 9));
    after->run (0, 0, { engineBlock }, [&] (size_t start) { after->controls.global().gateOpen = start >= frames / 2; });
    EXPECT_EQ (peak (after->mainOutputs(), 0, frames / 2), 0.0);
    EXPECT_GT (peak (after->mainOutputs(), frames / 2), 1e-4);

    // So does a re-prepared one.
    before.engine->prepare (engineRate, engineBlock);
    for (float gain : before.engine->gateLevels().gain)
        EXPECT_EQ (gain, 0.0f);
    before.controls.global().gateOpen = false;
    before.run();
    EXPECT_EQ (peak (before.mainOutputs()), 0.0);
}

TEST (EngineGate, HandoverKeepsTheLevel)
{
    const size_t frames = oneSecond + oneSecond / 2;
    const auto old = closedRig (calibratedConfig(), frames);
    constantStemCue (*old);
    old->run();
    const auto levels = old->engine->gateLevels();
    for (float gain : levels.gain)
        EXPECT_NEAR (gain, calibratedGain(), 1e-6);

    const auto swapped = closedRig (calibratedConfig(), oneSecond / 4);
    constantStemCue (*swapped);
    swapped->engine->startGateAt (levels);
    swapped->run();
    for (size_t i = 0; i < swapped->frames; ++i)
        ASSERT_NEAR (swapped->phones.l[i], phonesUnit() * calibratedGain(), 1e-6) << i;   // no ramp from 0
    for (int z = 0; z < a3::numGatedZones; ++z)
        EXPECT_EQ (swapped->engine->gateLevels().gain[z], levels.gain[z]);
}

TEST (EngineGate, ClosesFast)
{
    const size_t frames = oneSecond, change = 22016;   // a call boundary
    EngineRig rig (calibratedConfig(), frames);
    constantStemCue (rig);
    rig.setAnalogPair (1, pinkStereo (frames, 13));
    rig.run (0, 0, { engineBlock }, [&] (size_t start) { rig.controls.global().gateOpen = start < change; });

    EXPECT_NEAR (rig.phones.l[change - 1], phonesUnit() * calibratedGain(), 1e-6);
    EXPECT_GT (std::abs (rig.phones.l[change + fadeOut / 2]), 0.0f);   // a ramp, not a cut
    EXPECT_EQ (peak (stereo (rig.phones), change + fadeOut), 0.0);
    EXPECT_EQ (peak (rig.mainOutputs(), change + fadeOut), 0.0);
    EXPECT_EQ (peak (rig.boothOutputs(), change + fadeOut), 0.0);
    EXPECT_GT (peak (rig.recOutputs(), change + fadeOut), 1e-3);
}

// One meter per port, in the port order of Engine.h: each reads what its port carries.
TEST (EngineMeters, OnePerOutputPortInPortOrder)
{
    const size_t frames = 128 * 200;   // whole meter windows
    EngineRig rig (engineConfig (4, 2), frames);
    rig.setAnalogPair (1, pinkStereo (frames, 17));
    for (int ch = 0; ch < deckChannels; ++ch)
        rig.decks[0].bformat[(size_t) ch] = noise (frames, 0.1f, 80 + (uint32_t) ch).l;
    rig.controls.channel (2).source = 1;
    rig.run();

    std::vector<const std::vector<float>*> ports = rig.mainOutputs();
    for (const auto& list : { rig.boothOutputs(), rig.recOutputs(), stereo (rig.phones) })
        ports.insert (ports.end(), list.begin(), list.end());
    for (size_t c = 0; c < rig.pre.size(); ++c)
        for (const auto* s : { &rig.pre[c].l, &rig.pre[c].r, &rig.post[c].l, &rig.post[c].r })
            ports.push_back (s);

    const auto meters = rig.engine->outputMeters();
    ASSERT_EQ (meters.size(), ports.size());
    for (size_t p = 0; p < ports.size(); ++p)
    {
        EXPECT_FLOAT_EQ (meters[p].peak, (float) peak (*ports[p])) << "port " << p;
        EXPECT_EQ (meters[p].rms > 0.0f, peak (*ports[p]) > 0.0) << "port " << p;
    }
    EXPECT_GT (meters[0].peak, 0.0f);

    // The peaks were taken; the RMS stays.
    const auto again = rig.engine->outputMeters();
    for (size_t p = 0; p < ports.size(); ++p)
    {
        EXPECT_EQ (again[p].peak, 0.0f) << "port " << p;
        EXPECT_EQ (again[p].rms, meters[p].rms) << "port " << p;
    }
}
