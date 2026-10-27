// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// O13: the engine measures the meters and sends them to core.vu-relay; Core forwards an engine
// datagram, unchanged, to every display the analyzer used to reach (the truth's `core` routes that
// carry vu) and to the remote Motion and desks it follows, each endpoint once. StemDeck's datagrams
// (41-50) go to the followed ones alone, as before. No Python oracle: B1 has no such path.
#include <algorithm>
#include <cstring>

#include <gtest/gtest.h>

#include "CoreRig.h"
#include "EngineTruth.h"
#include "VuRelay.h"

using namespace a3core;
using namespace corerig;

namespace
{
const Truth& truth() { return enginetruth::truth(); }
const std::string& hash() { static const auto value = truth().fingerprint(); return value; }

void put32 (std::vector<uint8_t>& out, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back (static_cast<uint8_t> (value >> shift));
}

// "#bundle", the timetag "immediately", and one message per number: peak and rms.
std::vector<uint8_t> bundleOf (const std::vector<int>& numbers)
{
    std::vector<uint8_t> out { '#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    for (const auto number : numbers)
    {
        const auto message = encodeMessage ({ "/vu/" + std::to_string (number), { pyFloat (0.5), pyFloat (0.25) } });
        put32 (out, static_cast<uint32_t> (message.size()));
        out.insert (out.end(), message.begin(), message.end());
    }
    return out;
}

std::vector<uint8_t> messageOf (int number) { return encodeMessage ({ "/vu/" + std::to_string (number), { pyFloat (0.5), pyFloat (0.25) } }); }

const std::vector<uint8_t>& engineBundle() { static const auto data = bundleOf ({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }); return data; }
const std::vector<uint8_t>& stemBundle() { static const auto data = bundleOf ({ 41, 42, 43, 44, 45, 46, 47, 48, 49, 50 }); return data; }

// The rig's own Motion has spoken and then fallen silent for a minute: a remote one takes over, as in
// CorePeers.TowardsTheMotionItFollows.
void aRemoteMotionTakesOver (CoreRig& rig, const Endpoint& from)
{
    rig.motionHello (hash());
    rig.at (61.0);
    rig.tick();
    rig.hello (from, "motion", hash());
}

CoreRig::Options onTheNewTruth() { CoreRig::Options options; options.truth = &truth(); return options; }

std::vector<std::string> sorted (std::vector<std::string> items) { std::sort (items.begin(), items.end()); return items; }

const std::string motionVu = "127.0.0.1:7772";    // the rig's own Motion, a truth target
const std::string mixerVu = "192.168.8.11:7772";
const std::string radlaVu = "192.168.43.96:9001";
}

TEST (VuForward, TheSourceIsTheFirstMeterAndItsName)
{
    const auto of = [] (const std::vector<uint8_t>& data) { return vuSourceOf (truth(), data.data(), data.size()); };
    EXPECT_EQ (of (engineBundle()), VuSource::engine);
    EXPECT_EQ (of (stemBundle()), VuSource::stemdeck);
    EXPECT_EQ (of (messageOf (51)), VuSource::engine);      // a single message, not a bundle
    EXPECT_EQ (of (messageOf (48)), VuSource::stemdeck);
    EXPECT_EQ (of (messageOf (50)), VuSource::stemdeck);
    EXPECT_EQ (of (bundleOf ({ 41, 1 })), VuSource::stemdeck);   // the first one decides
    EXPECT_EQ (of (bundleOf ({ 1, 41 })), VuSource::engine);
}

TEST (VuForward, WhatIsNoMeterIsUnknown)
{
    const auto of = [] (const std::vector<uint8_t>& data) { return vuSourceOf (truth(), data.data(), data.size()); };
    EXPECT_EQ (of ({ 'x' }), VuSource::unknown);
    EXPECT_EQ (of ({}), VuSource::unknown);
    EXPECT_EQ (of (bundleOf ({})), VuSource::unknown);
    EXPECT_EQ (of (messageOf (0)), VuSource::unknown);
    EXPECT_EQ (of (messageOf (67)), VuSource::unknown);
    EXPECT_EQ (of (encodeMessage ({ "/beat", { pyInt (1) } })), VuSource::unknown);
}

TEST (VuForward, TheEngineTargetsAreTheCoreRoutesThatCarryVu)
{
    // motion.vu, mixer.osc and radla.vu: what the analyzer reached. Not desk.vu (the desks Core
    // follows are asked, not guessed), not the routes that carry no meters (motion.osc, ...).
    EXPECT_EQ (engineVuTargets (truth()), (std::vector<Endpoint> { { "127.0.0.1", 7772 }, { "192.168.8.11", 7772 }, { "192.168.43.96", 9001 } }));
}

TEST (VuForward, TheOldTruthHasNoEngineTargets)
{
    // B1's truth routes the meters from the analyzer: the parity suites see Core as it was.
    EXPECT_TRUE (engineVuTargets (b0::truth()).empty());
}

TEST (VuForward, EngineMetersReachEveryVuTargetOnce)
{
    CoreRig rig (CoreRig::WithOptions {}, onTheNewTruth());
    rig.motionHello (hash());   // the rig's own Motion: a truth target, never a vu_destination
    rig.clear();
    rig.vu (engineBundle());
    EXPECT_EQ (sorted (rig.targetsOf ("vu")), sorted ({ motionVu, mixerVu, radlaVu }));
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (VuForward, AnEndpointFollowedAndInTheTruthGetsItOnce)
{
    CoreRig rig (CoreRig::WithOptions {}, onTheNewTruth());
    aRemoteMotionTakesOver (rig, { "192.168.8.11", 40002 });   // it lives on the mixer's host
    rig.clear();
    rig.vu (engineBundle());
    EXPECT_EQ (sorted (rig.targetsOf ("vu")), sorted ({ motionVu, mixerVu, radlaVu }));
    const auto targets = rig.targetsOf ("vu");
    EXPECT_EQ (std::count (targets.begin(), targets.end(), mixerVu), 1);
}

TEST (VuForward, RemoteMotionAndDesksGetThemToo)
{
    CoreRig rig (CoreRig::WithOptions {}, onTheNewTruth());
    aRemoteMotionTakesOver (rig, rig.remoteMotion);
    rig.hello ({ "192.168.8.40", 7773 }, "desk", hash());
    rig.hello ({ "192.168.8.41", 7773 }, "desk", hash());
    rig.clear();
    rig.vu (engineBundle());
    EXPECT_EQ (sorted (rig.targetsOf ("vu")),
               sorted ({ motionVu, mixerVu, radlaVu, "192.168.8.20:7772", "192.168.8.40:7774", "192.168.8.41:7774" }));
}

TEST (VuForward, StemDeckMetersAsToday)
{
    // The followed ones alone, as B1's relay passes StemDeck's: nothing for the truth's displays.
    CoreRig rig (CoreRig::WithOptions {}, onTheNewTruth());
    aRemoteMotionTakesOver (rig, rig.remoteMotion);
    rig.hello ({ "192.168.8.40", 7773 }, "desk", hash());
    rig.clear();
    rig.vu (stemBundle());
    EXPECT_EQ (sorted (rig.targetsOf ("vu")), sorted ({ "192.168.8.20:7772", "192.168.8.40:7774" }));
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (VuForward, ForwardedUnchanged)
{
    CoreRig rig (CoreRig::WithOptions {}, onTheNewTruth());
    rig.hello ({ "192.168.8.40", 7773 }, "desk", hash());
    rig.clear();
    rig.vu (engineBundle());
    ASSERT_EQ (rig.net.transcript.size(), 4u);   // three truth targets and the desk
    const auto sent = juce::Base64::toBase64 (engineBundle().data(), engineBundle().size());
    for (const auto& entry : rig.net.transcript)
        EXPECT_EQ (entry["raw"].toString(), sent);
}

TEST (VuForward, NoEngineNoEngineMeters)
{
    // Nothing arrives on core.vu-relay: nothing is sent, and nothing is made up from the truth.
    CoreRig rig (CoreRig::WithOptions {}, onTheNewTruth());
    rig.hello ({ "192.168.8.40", 7773 }, "desk", hash());
    rig.clear();
    for (double at = 1.0; at <= 10.0; at += 1.0)
    {
        rig.at (at);
        rig.tick();
    }
    EXPECT_TRUE (rig.targetsOf ("vu").empty());
}

TEST (VuForward, WhatIsNoMeterIsNotedAndGoesOnToTheFollowedAsBefore)
{
    CoreRig rig (CoreRig::WithOptions {}, onTheNewTruth());
    rig.hello ({ "192.168.8.40", 7773 }, "desk", hash());
    rig.clear();
    rig.vu ({ 'x', 'y' });
    EXPECT_EQ (rig.targetsOf ("vu"), (std::vector<std::string> { "192.168.8.40:7774" }));
    EXPECT_EQ (rig.unknownRows(), 1u);
}
