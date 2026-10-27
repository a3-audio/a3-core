// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The /vu datagrams as beat-analyzer sends them. The first five are its tests/vu_bundle_test.cpp, the
// address cases its tests/vu_ports_test.cpp (7dd100b), ported by name; ByteEqualToTheAnalyzer holds the
// bytes its own serializer made (tests/engine/data/vu-bundles.json, recorded from its sources).
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "VuBundles.h"
#include "VuTaps.h"
#include "VuTestData.h"

namespace
{
int readInt32 (const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

// How many messages a serialised bundle holds.
int elementsIn (const std::vector<uint8_t>& bundle)
{
    size_t pos = 16;
    int n = 0;
    while (pos + 4 <= bundle.size())
    {
        pos += 4 + (size_t) readInt32 (bundle.data() + pos);
        ++n;
    }
    return n;
}

// The addresses of a bundle's messages, in order.
std::vector<std::string> addressesIn (const std::vector<uint8_t>& bundle)
{
    std::vector<std::string> list;
    for (size_t pos = 16; pos + 4 <= bundle.size();)
    {
        const auto length = (size_t) readInt32 (bundle.data() + pos);
        list.emplace_back (reinterpret_cast<const char*> (bundle.data() + pos + 4));
        pos += 4 + length;
    }
    return list;
}

// Every bundle of `count` channels fits the analyzer's 512-byte slot and carries its channels.
int sendAll (const std::vector<std::string>& paths, int count)
{
    const std::vector<float> peaks ((size_t) count, 0.5f), rms ((size_t) count, 0.25f);
    int total = 0;
    for (const auto& [first, size] : a3::vuBundleChunks (count))
    {
        const auto bundle = a3::serializeVuBundle (paths.data() + first, peaks.data() + first, rms.data() + first, size);
        EXPECT_GT (bundle.size(), 0u);
        EXPECT_LE (bundle.size(), 512u);
        EXPECT_EQ (elementsIn (bundle), size);
        total += size;
    }
    return total;
}

std::vector<uint8_t> bytesOfHex (const juce::String& hex)
{
    juce::MemoryBlock block;
    block.loadFromHexString (hex);
    const auto* data = static_cast<const uint8_t*> (block.getData());
    return std::vector<uint8_t> (data, data + block.getSize());
}

std::vector<float> floatsOf (const juce::var& list)
{
    std::vector<float> values;
    for (int i = 0; i < list.size(); ++i)
        values.push_back ((float) (double) list[i]);
    return values;
}
}

TEST (VuBundles, FortyChannelsGoOutOneBundlePerBlock)
{
    const auto chunks = a3::vuBundleChunks (40);
    ASSERT_EQ (chunks.size(), 4u);
    for (int b = 0; b < 4; ++b)
        EXPECT_EQ (chunks[(size_t) b], std::make_pair (b * 10, 10));
}

TEST (VuBundles, EveryBundleFitsAndCarriesItsChannels)
{
    std::vector<std::string> paths;
    for (int i = 0; i < 40; ++i)
        paths.push_back ("/vu/" + std::to_string (i));
    EXPECT_EQ (sendAll (paths, 40), 40);
}

TEST (VuBundles, AShortLastBlockIsItsOwnBundle)
{
    const auto chunks = a3::vuBundleChunks (12);
    ASSERT_EQ (chunks.size(), 2u);
    EXPECT_EQ (chunks[0], std::make_pair (0, 10));
    EXPECT_EQ (chunks[1], std::make_pair (10, 2));
    EXPECT_TRUE (a3::vuBundleChunks (0).empty());
}

TEST (VuBundles, ABundleOfFortyEightFits)
{
    std::vector<std::string> paths;
    for (int i = 0; i < 48; ++i)
        paths.push_back ("/vu/" + std::to_string (i + 1));
    const auto chunks = a3::vuBundleChunks (48);
    ASSERT_EQ (chunks.size(), 5u);
    EXPECT_EQ (chunks[4], std::make_pair (40, 8));
    EXPECT_EQ (sendAll (paths, 48), 48);
}

TEST (VuBundles, TheStereoChannelMetersAreTwoBundlesOfEight)
{
    const auto chunks = a3::vuBundleChunks (56);
    ASSERT_EQ (chunks.size(), 6u);
    EXPECT_EQ (chunks[4], std::make_pair (40, 8));
    EXPECT_EQ (chunks[5], std::make_pair (48, 8));
    std::vector<std::string> paths;
    for (int i = 0; i < 56; ++i)
        paths.push_back ("/vu/" + std::to_string (i < 40 ? i + 1 : i + 11));
    EXPECT_EQ (sendAll (paths, 56), 56);
}

TEST (VuBundles, TheStereoChannelMetersAreSentAs51To66)
{
    EXPECT_EQ (a3::vuAddress ("/vu/{n}", 40), "/vu/51");   // in1_pre_L
    EXPECT_EQ (a3::vuAddress ("/vu/{n}", 47), "/vu/58");   // in4_pre_R
    EXPECT_EQ (a3::vuAddress ("/vu/{n}", 48), "/vu/59");   // in1_post_L
    EXPECT_EQ (a3::vuAddress ("/vu/{n}", 55), "/vu/66");   // in4_post_R
}

TEST (VuBundles, TheOscAddressCountsFromOne)
{
    EXPECT_EQ (a3::vuAddress ("/vu/{n}", 0), "/vu/1");     // analog1_L
    EXPECT_EQ (a3::vuAddress ("/vu/{n}", 10), "/vu/11");   // main_sub
    EXPECT_EQ (a3::vuAddress ("/vu/{n}", 39), "/vu/40");   // free70, the last of the forty
}

// /vu/41-50 are StemDeck's: one send of the engine carries 1-40 and 51-66, each once, in six bundles.
TEST (VuBundles, TheVuInputsNeverReachStemdecksAddresses)
{
    const auto pattern = a3test::truthVuPattern();
    const std::vector<float> peaks (56, 0.5f), rms (56, 0.25f);
    const auto bundles = a3::vuBundles (pattern, peaks, rms);
    ASSERT_EQ (bundles.size(), 6u);
    std::vector<std::string> sent;
    for (const auto& bundle : bundles)
        for (const auto& address : addressesIn (bundle))
            sent.push_back (address);
    std::vector<std::string> expected;
    for (int n = 1; n <= 40; ++n)
        expected.push_back ("/vu/" + std::to_string (n));
    for (int n = 51; n <= 66; ++n)
        expected.push_back ("/vu/" + std::to_string (n));
    EXPECT_EQ (sent, expected);
}

TEST (VuBundles, ByteEqualToTheAnalyzer)
{
    const auto recorded = a3test::jsonFile (A3_VU_BUNDLES);
    const auto peaks = floatsOf (recorded["peaks"]);
    const auto rms = floatsOf (recorded["rms"]);
    ASSERT_EQ (peaks.size(), 56u);
    ASSERT_EQ (rms.size(), 56u);
    EXPECT_EQ (recorded["pattern"].toString().toStdString(), a3test::truthVuPattern());

    const auto bundles = a3::vuBundles (recorded["pattern"].toString().toStdString(), peaks, rms);
    const auto expected = recorded["bundles"];
    ASSERT_EQ ((int) bundles.size(), expected.size());
    for (int b = 0; b < expected.size(); ++b)
        EXPECT_EQ (bundles[(size_t) b], bytesOfHex (expected[b].toString())) << "bundle " << b;
}
