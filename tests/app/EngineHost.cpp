// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <vector>
#include <gtest/gtest.h>
#include "AppTruth.h"
#include "EngineFixture.h"
#include "EngineHost.h"

namespace
{
constexpr double rate = a3test::engineRate;
constexpr int maxCall = 2048;
constexpr int tickEvery = 882;   // the app's 50 Hz control timer, in samples

int portsOf (const a3::Layout& layout) { return (layout.hasSub() ? 1 : 0) + (int) layout.topChannels.size(); }

// The host driven as a backend drives it: audio callbacks of a chosen size with a
// 997 Hz sine on analog pair 1, the control tick every 20 ms of audio.
struct HostRig
{
    a3::EngineConfig config = a3test::engineConfig();
    a3::SpatialConfig spatial = a3test::loadedSpatialConfig (a3test::shippedSpatialDirectory);
    std::vector<juce::OSCMessage> toCore;
    a3app::EngineHost host { config, spatial, a3apptest::engineTruth(),
                             [this] (const juce::OSCMessage& m) { toCore.push_back (m); } };
    bool ticking = true;
    long frame = 0;
    std::vector<float> left = std::vector<float> (maxCall), right = std::vector<float> (maxCall);
    std::vector<std::vector<float>> main;
    float lastMainPeak = 0.0f;

    HostRig() : main ((size_t) portsOf (spatial.main.layout), std::vector<float> (maxCall)) {}

    void restValues()
    {
        auto& c = host.controls();
        for (int n = 0; n < c.channels(); ++n)
        {
            auto& ch = c.channel (n);
            ch.gain = 0.75f; ch.volume = 0.75f;
            ch.eqHigh = ch.eqMid = ch.eqLow = 0.5f;
            ch.frequency = 0.5f; ch.q = 0.5f; ch.threeD = 1.0f;
        }
        auto& g = c.global();
        g.masterVolume = a3test::masterAtRest; g.booth = a3test::boothAtRest;
        g.phonesVolume = 0.75f; g.auxReturn = 0.75f;
        g.gateOpen = true;
    }

    void callback (int n)
    {
        for (int i = 0; i < n; ++i)
            left[(size_t) i] = right[(size_t) i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * (double) (frame + i) / rate);
        a3::EngineInputs in;
        in.analog = { left.data(), right.data() };
        a3::EngineOutputs out;
        size_t port = 0;
        if (spatial.main.layout.hasSub())
            out.main.sub = main[port++].data();
        while (port < main.size())
            out.main.tops.push_back (main[port++].data());
        host.audioCallback (in, out, n);

        lastMainPeak = 0.0f;
        for (const auto& channel : main)
            for (int i = 0; i < n; ++i)
                lastMainPeak = std::max (lastMainPeak, std::abs (channel[(size_t) i]));
        const long before = frame;
        frame += n;
        if (ticking && before / tickEvery != frame / tickEvery)
            host.controlTick ((double) frame / rate);
    }

    void run (double seconds, int callSize)
    {
        for (long done = 0; done < (long) (seconds * rate); done += callSize)
            callback (callSize);
    }

    float mainGate() const { return host.gateLevels().gain[(int) a3::GatedZone::main]; }
};
}

TEST (EngineHost, SilenceWhileNoEngineRuns)
{
    HostRig rig;
    for (auto& channel : rig.main)
        std::fill (channel.begin(), channel.end(), 1.0f);
    rig.callback (128);
    EXPECT_EQ (rig.lastMainPeak, 0.0f);
    EXPECT_EQ (rig.host.engine(), nullptr);
    EXPECT_EQ (rig.host.preparedBlockSize(), 0);
    EXPECT_EQ (rig.mainGate(), 0.0f);
}

TEST (EngineHost, SwapKeepsControlsAndGate)
{
    HostRig rig;
    ASSERT_TRUE (rig.host.rebuild (rate, 128, false).wasOk());
    rig.restValues();
    rig.host.controls().channel (0).eqHigh = 0.3f;
    rig.run (1.2, 128);
    ASSERT_FLOAT_EQ (rig.mainGate(), 1.0f) << "open at the 0 dB trim after 1 s";

    const float peakBefore = rig.lastMainPeak;
    ASSERT_GT (peakBefore, 0.001f);

    // A quantum change: the new Engine takes over at once, with the same controls and the open gate.
    const auto* controlsBefore = &rig.host.controls();
    ASSERT_TRUE (rig.host.rebuild (rate, 256, false).wasOk());
    EXPECT_EQ (rig.host.preparedBlockSize(), 256);
    EXPECT_EQ (&rig.host.controls(), controlsBefore);
    EXPECT_EQ (rig.host.controls().channel (0).eqHigh.load(), 0.3f);
    EXPECT_FLOAT_EQ (rig.mainGate(), 1.0f) << "handed over, not re-faded";
    rig.run (0.1, 256);
    EXPECT_FLOAT_EQ (rig.mainGate(), 1.0f);
    for (const auto zone : { a3::GatedZone::booth, a3::GatedZone::phones })
        EXPECT_FLOAT_EQ (rig.host.gateLevels().gain[(int) zone], 1.0f);
    EXPECT_NEAR (rig.lastMainPeak, peakBefore, 0.05f * peakBefore) << "the level runs on";

    // A Save: the running Engine fades out, the new one takes over closed and opens over 1 s.
    ASSERT_TRUE (rig.host.rebuild (rate, 64, true).wasOk());
    EXPECT_EQ (rig.host.preparedBlockSize(), 256) << "the old Engine runs until it is silent";
    long swapAt = -1;
    float previous = 1.0f;
    std::vector<float> gateAfterSwap;
    for (long done = 0; done < (long) (2.5 * rate); done += 64)
    {
        rig.callback (64);
        if (swapAt < 0)
        {
            EXPECT_LE (rig.mainGate(), previous) << "fading out at " << done;
            previous = rig.mainGate();
            if (rig.host.preparedBlockSize() == 64)
            {
                swapAt = done + 64;
                EXPECT_EQ (rig.mainGate(), 0.0f) << "the new Engine starts closed";
            }
            continue;
        }
        gateAfterSwap.push_back (rig.mainGate());
    }
    ASSERT_GE (swapAt, 0) << "never swapped";
    EXPECT_LE (swapAt, (long) (0.050 * rate)) << "faded out and swapped within 50 ms";
    EXPECT_TRUE (rig.host.controls().global().gateOpen.load()) << "Core's open is not touched by the fade";

    const auto gateAt = [&] (double secondsAfterSwap) { return gateAfterSwap[(size_t) (secondsAfterSwap * rate / 64)]; };
    EXPECT_NEAR (gateAt (0.5), 0.5f, 0.02f) << "linear over 1 s";
    EXPECT_LT (gateAt (0.95), 1.0f);
    EXPECT_FLOAT_EQ (gateAt (1.01), 1.0f);
    EXPECT_FLOAT_EQ (gateAfterSwap.back(), 1.0f);
}

TEST (EngineHost, ControlTickRunsTheModulation)
{
    HostRig rig;
    rig.ticking = false;
    ASSERT_TRUE (rig.host.rebuild (rate, 128, false).wasOk());
    rig.restValues();
    auto* pitch = rig.host.engine()->delay().rawParameter ("pitchL");
    ASSERT_NE (pitch, nullptr);
    const float before = pitch->load();
    rig.run (0.5, 128);
    EXPECT_EQ (pitch->load(), before) << "the audio callback never runs the modulation";
    EXPECT_TRUE (rig.toCore.empty());
    EXPECT_TRUE (rig.host.meters().empty());

    rig.host.controlTick (3.7);
    EXPECT_NE (pitch->load(), before);

    ASSERT_EQ (rig.toCore.size(), 1u) << "the hello";
    EXPECT_EQ (rig.toCore[0].getAddressPattern().toString(), "/engine/hello");
    EXPECT_EQ (rig.toCore[0][0].getString(), rig.host.bootId());

    const auto meters = rig.host.meters();
    EXPECT_EQ ((int) meters.size(), portsOf (rig.spatial.main.layout) + portsOf (rig.spatial.booth.layout) + 4
                                        + 4 * rig.config.channels);
    EXPECT_GT (meters[0].peak + meters[1].peak, 0.0f) << "main carries the sine";
}
