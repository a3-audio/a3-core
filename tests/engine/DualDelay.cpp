// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cstdlib>
#include <unistd.h>
#include "DelayModulation.h"
#include "IemNode.h"
#include "SpatialConfig.h"
#include "SpatialTestData.h"

namespace
{
using Channels = std::vector<std::vector<float>>;
constexpr int busChannels = 16;
constexpr double sampleRate = 44100.0;
constexpr int blockSize = 64;

juce::var shippedFx()
{
    return juce::JSON::parse (a3test::shippedSpatialDirectory.getChildFile ("fx.parameters.json").loadFileAsString());
}

std::unique_ptr<a3::IemNode> makeDualDelay()
{
    auto node = a3::IemNode::create (a3::IemKind::dualDelay, busChannels, busChannels);
    EXPECT_TRUE (node != nullptr);
    juce::String error;
    const auto config = a3::loadSpatialConfig (a3test::shippedSpatialDirectory, error);
    EXPECT_TRUE (config.has_value()) << error;
    EXPECT_TRUE (node->setParameters (config->fxParameters).wasOk());
    return node;
}

// Feeds `input` (channel 0 only) in blocks and returns channel 0 of the output.
std::vector<float> render (a3::IemNode& node, const std::vector<float>& input)
{
    std::vector<float> out (input.size());
    juce::AudioBuffer<float> io (busChannels, blockSize);
    for (size_t start = 0; start < input.size(); start += blockSize)
    {
        const int n = (int) std::min ((size_t) blockSize, input.size() - start);
        io.clear();
        io.copyFrom (0, 0, input.data() + start, n);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), busChannels, 0, n);
        node.process (view);
        std::copy_n (io.getReadPointer (0), n, out.begin() + (long) start);
    }
    return out;
}

// Sample index of the largest |x| at or after `from`.
size_t peakAfter (const std::vector<float>& x, size_t from)
{
    size_t best = from;
    for (size_t i = from; i < x.size(); ++i)
        if (std::abs (x[i]) > std::abs (x[best]))
            best = i;
    return best;
}

size_t firstEchoAt (float bpm)
{
    auto node = makeDualDelay();
    node->prepare (sampleRate, blockSize);
    a3::DelayLoop loop (*node);
    loop.prepare (sampleRate);
    loop.set (bpm, a3::delay::defaultBeat, a3::delay::defaultFeedback, blockSize);   // the time is held until the next set
    render (*node, std::vector<float> (44100 * 2, 0.0f));   // let the delay time settle
    std::vector<float> impulse (44100, 0.0f);
    impulse[0] = 1.0f;
    const auto out = render (*node, impulse);
    return peakAfter (out, 1000);
}
}

TEST (DualDelay, NeverListensForOscAndWritesNothingUnderHome)
{
    const auto fakeHome = juce::File ("/home/aaa/a3-system/wt").getChildFile ("dualdelay-home-test-" + juce::String (getpid()));
    ASSERT_TRUE (fakeHome.createDirectory().wasOk());
    const char* oldHome = std::getenv ("HOME");
    const std::string saved = oldHome != nullptr ? oldHome : "";
    ASSERT_EQ (setenv ("HOME", fakeHome.getFullPathName().toRawUTF8(), 1), 0);
    {
        auto node = makeDualDelay();
        node->prepare (sampleRate, blockSize);
        render (*node, std::vector<float> (6400, 0.1f));
        EXPECT_FALSE (node->listensForOsc());
    }
    setenv ("HOME", saved.c_str(), 1);
    EXPECT_EQ (fakeHome.findChildFiles (juce::File::findFilesAndDirectories, true).size(), 0);
    fakeHome.deleteRecursively();
}

TEST (DualDelay, ImpulseEchoesAtTheTempo)
{
    // The default quarter beat (R1, what delayMult 4 played): one echo every 60 / (bpm * 4) seconds.
    const double expected120 = sampleRate * 60.0 / (120.0 * 4.0);
    const double expected90 = sampleRate * 60.0 / (90.0 * 4.0);
    EXPECT_NEAR ((double) firstEchoAt (120.0f), expected120, blockSize);
    EXPECT_NEAR ((double) firstEchoAt (90.0f), expected90, blockSize);
}

TEST (DualDelay, DataParametersAreLegal)
{
    auto node = makeDualDelay();
    const auto fx = shippedFx();
    const auto* object = fx.getDynamicObject();
    ASSERT_TRUE (object != nullptr);
    for (const auto& property : object->getProperties())
    {
        const auto id = property.name.toString();
        ASSERT_TRUE (node->rawParameter (id) != nullptr) << "unknown id: " << id.toRawUTF8();
        const float value = (float) (double) property.value;
        EXPECT_NEAR (node->legalValue (id, value), value, 1e-3f * std::max (1.0f, std::abs (value))) << id.toStdString();
    }
}

TEST (DualDelay, TheEngineChoosesThirdOrderAndNoSync)
{
    auto node = makeDualDelay();
    EXPECT_FLOAT_EQ (node->rawParameter ("orderSetting")->load(), 4.0f);
    EXPECT_FLOAT_EQ (node->rawParameter ("syncL")->load(), 0.0f);
    EXPECT_FLOAT_EQ (node->rawParameter ("syncR")->load(), 0.0f);
    EXPECT_FLOAT_EQ (node->rawParameter ("lfoDepthL")->load(), 0.0f);
}

// R5: SPREAD turns the lines, so both rotate (warp left yaw and pitch inert) at the default spread,
// with no tilt; the cutoffs are COLOR's centre (R4).
TEST (DualDelay, TheShippedDataRotatesAtTheDefaultSpread)
{
    auto node = makeDualDelay();
    for (const auto& [id, value] : std::initializer_list<std::pair<const char*, float>> {
             { "transformModeL", 0.0f }, { "transformModeR", 0.0f }, { "pitchL", 0.0f }, { "pitchR", 0.0f },
             { "rollL", 0.0f }, { "rollR", 0.0f }, { "yawL", 60.0f }, { "yawR", -60.0f },
             { "HPcutOffL", 233.0f }, { "HPcutOffR", 233.0f }, { "LPcutOffL", 8000.0f }, { "LPcutOffR", 8000.0f } })
        EXPECT_FLOAT_EQ (node->rawParameter (id)->load(), value) << id;
}

// The line's floor in DelayParameters is DualDelay's own: its legal delayBPM starts there.
TEST (DualDelay, TheLineIsAsLongAsDelayParametersSays)
{
    auto node = makeDualDelay();
    EXPECT_FLOAT_EQ (node->legalValue ("delayBPML", 0.0f), a3::delay::lowestDelayBpm);
    EXPECT_FLOAT_EQ (node->legalValue ("delayMultL", 0.0f), 1.0f);
}
