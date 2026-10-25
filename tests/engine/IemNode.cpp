// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cstdlib>
#include <unistd.h>
#include "IemNode.h"
#include "TestSignals.h"

namespace
{
using Channels = std::vector<std::vector<float>>;

constexpr int busChannels = 16;

juce::String quadraphonicConfig()
{
    return juce::File (A3_IEM_PRESETS_DIR).getChildFile ("Quadraphonic.json").loadFileAsString();
}

std::unique_ptr<a3::IemNode> makeEncoder()
{
    auto node = a3::IemNode::create (a3::IemKind::multiEncoder, 8, busChannels);
    EXPECT_TRUE (node != nullptr);
    EXPECT_TRUE (node->setParameters (juce::JSON::parse (R"({"inputSetting":8,"orderSetting":4,"useSN3D":1})")).wasOk());
    return node;
}

std::unique_ptr<a3::IemNode> makeSimpleDecoder (bool withConfig = true)
{
    auto node = a3::IemNode::create (a3::IemKind::simpleDecoder, busChannels, 5);
    EXPECT_TRUE (node != nullptr);
    if (withConfig)
        EXPECT_TRUE (node->loadDecoderConfig (quadraphonicConfig()).wasOk());
    return node;
}

std::unique_ptr<a3::IemNode> makeBinauralDecoder()
{
    auto node = a3::IemNode::create (a3::IemKind::binauralDecoder, busChannels, 2);
    EXPECT_TRUE (node != nullptr);
    return node;
}

Channels silence (int channels, size_t frames)
{
    return Channels ((size_t) channels, std::vector<float> (frames, 0.0f));
}

// Feeds `input` through the node in calls of the given sizes (cycled) and returns the first `outChannels`.
Channels render (a3::IemNode& node, const Channels& input, int outChannels, const std::vector<int>& callSizes)
{
    const size_t frames = input[0].size();
    Channels out = silence (outChannels, frames);
    juce::AudioBuffer<float> io (busChannels, node.preparedBlockSize());
    size_t start = 0, call = 0;
    while (start < frames)
    {
        const int n = (int) std::min ((size_t) callSizes[call++ % callSizes.size()], frames - start);
        io.clear();
        for (size_t c = 0; c < input.size(); ++c)
            io.copyFrom ((int) c, 0, input[c].data() + start, n);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), busChannels, 0, n);
        node.process (view);
        for (int c = 0; c < outChannels; ++c)
            std::copy_n (io.getReadPointer (c), n, out[(size_t) c].begin() + (long) start);
        start += (size_t) n;
    }
    return out;
}

double rms (const std::vector<float>& x)
{
    double sum = 0.0;
    for (float v : x)
        sum += (double) v * v;
    return std::sqrt (sum / (double) x.size());
}

bool allFinite (const Channels& c)
{
    for (const auto& ch : c)
        for (float v : ch)
            if (! std::isfinite (v))
                return false;
    return true;
}

Channels wOnly (const std::vector<float>& w)
{
    Channels in = silence (busChannels, w.size());
    in[0] = w;
    return in;
}

Channels noiseOnAllChannels (size_t frames)
{
    Channels in;
    for (int c = 0; c < busChannels; ++c)
        in.push_back (a3test::noise (frames, 0.3f, (uint32_t) (100 + c)).l);
    return in;
}
}

TEST (IemNode, NeverListensForOsc)
{
    auto encoder = makeEncoder();
    auto simple = makeSimpleDecoder();
    auto binaural = makeBinauralDecoder();
    for (auto* node : { encoder.get(), simple.get(), binaural.get() })
    {
        node->prepare (44100.0, 64);
        render (*node, silence (busChannels, 640), 2, { 64 });
        EXPECT_FALSE (node->listensForOsc());
    }
}

TEST (IemNode, WritesNothingUnderHome)
{
    const auto fakeHome = juce::File ("/home/aaa/a3-system/wt").getChildFile ("iem-node-home-test-" + juce::String (getpid()));
    ASSERT_TRUE (fakeHome.createDirectory().wasOk());
    const char* oldHome = std::getenv ("HOME");
    const std::string saved = oldHome != nullptr ? oldHome : "";
    ASSERT_EQ (setenv ("HOME", fakeHome.getFullPathName().toRawUTF8(), 1), 0);
    {
        auto encoder = makeEncoder();
        auto simple = makeSimpleDecoder();
        auto binaural = makeBinauralDecoder();
        for (auto* node : { encoder.get(), simple.get(), binaural.get() })
        {
            node->prepare (44100.0, 64);
            render (*node, noiseOnAllChannels (640), 2, { 64 });
        }
    }
    setenv ("HOME", saved.c_str(), 1);
    const auto leftovers = fakeHome.findChildFiles (juce::File::findFilesAndDirectories, true);
    fakeHome.deleteRecursively();
    EXPECT_EQ (leftovers.size(), 0) << leftovers[0].getFullPathName();
}

TEST (IemNode, SilenceStaysSilent)
{
    auto encoder = makeEncoder();
    auto simple = makeSimpleDecoder();
    auto binaural = makeBinauralDecoder();
    const Channels zeros = silence (busChannels, 2 * 44100);
    for (auto* node : { encoder.get(), simple.get(), binaural.get() })
    {
        node->prepare (44100.0, 64);
        for (const auto& channel : render (*node, zeros, busChannels, { 64 }))
            for (float v : channel)
                ASSERT_EQ (v, 0.0f);
    }
}

TEST (IemNode, UnknownParameterIsRefused)
{
    auto node = makeEncoder();
    const auto result = node->setParameter ("noSuchId", 1.0f);
    EXPECT_TRUE (result.failed());
    EXPECT_TRUE (result.getErrorMessage().contains ("noSuchId"));
}

TEST (IemNode, SetParametersAppliesNothingWhenOneEntryIsBad)
{
    auto node = makeEncoder();
    const float before = node->rawParameter ("gain0")->load();
    for (const char* json : { R"({"gain0":-3,"noSuchId":1})", R"({"gain0":-3,"azimuth0":"left"})",
                              R"({"gain0":-3,"azimuth0":{"a":1}})" })
    {
        const auto result = node->setParameters (juce::JSON::parse (json));
        EXPECT_TRUE (result.failed()) << json;
        EXPECT_EQ (node->rawParameter ("gain0")->load(), before) << json;
    }
    EXPECT_TRUE (node->setParameters (juce::JSON::parse (R"({"gain0":-3,"azimuth0":true})")).wasOk());
    EXPECT_EQ (node->rawParameter ("gain0")->load(), node->legalValue ("gain0", -3.0f));
}

TEST (IemNode, DecoderConfigIsRefusedOnTheWrongKindAndOnBadJson)
{
    auto encoder = makeEncoder();
    EXPECT_TRUE (encoder->loadDecoderConfig (quadraphonicConfig()).failed());

    auto simple = makeSimpleDecoder (false);
    const auto bad = simple->loadDecoderConfig ("{");
    EXPECT_TRUE (bad.failed());
    EXPECT_TRUE (bad.getErrorMessage().isNotEmpty());
    EXPECT_TRUE (simple->loadDecoderConfig ("").failed());
    EXPECT_TRUE (simple->loadDecoderConfig ("{}").failed());
}

TEST (IemNode, LoadingASubLessConfigKeepsTheSubMode)
{
    ASSERT_FALSE (quadraphonicConfig().contains ("SubwooferChannel"));
    auto node = makeSimpleDecoder (false);
    ASSERT_TRUE (node->setParameters (juce::JSON::parse (R"({"swMode":1,"swChannel":1})")).wasOk());
    node->prepare (44100.0, 64);
    ASSERT_TRUE (node->loadDecoderConfig (quadraphonicConfig()).wasOk());

    const auto in = wOnly (a3test::sine (8192, 50.0, 44100.0, 0.5f).l);
    const auto out = render (*node, in, 5, { 64 });
    EXPECT_EQ (node->rawParameter ("swMode")->load(), 1.0f);
    EXPECT_EQ (node->rawParameter ("swChannel")->load(), 1.0f);
    const std::vector<float> tail (out[0].begin() + 4096, out[0].end());
    EXPECT_GT (rms (tail), 1e-3);
}

TEST (IemNode, BinauralAt48kIsNotSilentAndFinite)
{
    const auto levelAt = [] (double sampleRate, std::vector<double>& rmsPerEar)
    {
        auto node = makeBinauralDecoder();
        node->prepare (sampleRate, 64);
        const auto out = render (*node, wOnly (a3test::noise ((size_t) sampleRate, 0.3f, 7).l), 2, { 64 });
        EXPECT_TRUE (allFinite (out));
        rmsPerEar = { rms (out[0]), rms (out[1]) };
    };
    std::vector<double> at44, at48;
    levelAt (44100.0, at44);
    levelAt (48000.0, at48);
    for (size_t ear = 0; ear < 2; ++ear)
    {
        EXPECT_GT (at48[ear], 1e-4);
        EXPECT_NEAR (20.0 * std::log10 (at48[ear] / at44[ear]), 0.0, 1.0);
    }
}

TEST (IemNode, PositionWrittenRawEqualsPositionSetNotifying)
{
    auto notifying = makeEncoder();
    auto raw = makeEncoder();
    notifying->prepare (44100.0, 64);
    raw->prepare (44100.0, 64);
    ASSERT_TRUE (notifying->setParameter ("azimuth0", 37.5f).wasOk());
    raw->rawParameter ("azimuth0")->store (raw->legalValue ("azimuth0", 37.5f));

    Channels in = silence (8, 64);
    in[0] = a3test::noise (64, 0.3f, 3).l;
    const auto a = render (*notifying, in, busChannels, { 64 });
    const auto b = render (*raw, in, busChannels, { 64 });
    for (int c = 0; c < busChannels; ++c)
        for (size_t i = 0; i < 64; ++i)
            ASSERT_NEAR (a[(size_t) c][i], b[(size_t) c][i], 1e-6f);
    EXPECT_GT (rms (a[1]), 1e-3);   // az 37.5 deg puts energy on Y: the position took effect
}

TEST (IemNode, AnyCallSizeUpToThePreparedBlock)
{
    const auto input = noiseOnAllChannels (2048);
    const std::vector<std::function<std::unique_ptr<a3::IemNode>()>> makers {
        [] { return makeSimpleDecoder(); }, [] { return makeBinauralDecoder(); } };
    for (const auto& make : makers)
    {
        auto reference = make();
        auto ragged = make();
        reference->prepare (44100.0, 64);
        ragged->prepare (44100.0, 64);
        const auto a = render (*reference, input, 2, { 64 });
        const auto b = render (*ragged, input, 2, { 1, 7, 63, 64, 13, 64 });
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 256; i < a[c].size(); ++i)
                ASSERT_NEAR (a[c][i], b[c][i], 1e-6f) << "channel " << c << " frame " << i;
    }
}
