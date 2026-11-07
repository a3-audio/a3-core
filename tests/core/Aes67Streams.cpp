// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The Core's AES67 transmit streams: the truth's table resolved against the live speaker layout.
#include <gtest/gtest.h>

#include <functional>
#include <regex>
#include <tuple>

#include "Aes67Streams.h"

using namespace a3core;

namespace
{
using Names = std::vector<std::string>;

const Truth& shippedTruth()
{
    static const auto truth = std::get<Truth> (Truth::load (juce::File (A3_PACKAGE_TRUTH), std::nullopt));
    return truth;
}

a3::SpatialConfig loadedSpatial (const char* directory)
{
    juce::String error;
    const auto spatial = a3::loadSpatialConfig (juce::File (directory), error);
    EXPECT_TRUE (spatial.has_value()) << error;
    return spatial.value_or (a3::SpatialConfig {});
}

// The package's default (data/spatial, "Default 4.1").
a3::SpatialConfig shippedSpatial() { return loadedSpatial (A3_SPATIAL_DATA); }

// A 6.1 in both zones: the engine tests' REAPER booth copy has it in the booth; main takes the same.
a3::SpatialConfig sixOneSpatial()
{
    auto spatial = loadedSpatial (A3_SPATIAL_REAPER_BOOTH_DATA);
    spatial.main.layout = spatial.booth.layout;
    return spatial;
}

a3::Layout layout (int subChannel, int tops)
{
    a3::Layout made;
    made.subChannel = subChannel;
    for (int k = 1; k <= tops; ++k)
        made.topChannels.push_back (k + (subChannel > 0 ? 1 : 0));
    return made;
}

a3::SpatialConfig spatialWith (const a3::Layout& main, const a3::Layout& booth)
{
    a3::SpatialConfig spatial;
    spatial.main.layout = main;
    spatial.booth.layout = booth;
    return spatial;
}

const StreamSpec& stream (const std::vector<StreamSpec>& streams, const std::string& key)
{
    for (const auto& candidate : streams)
        if (candidate.key == key)
            return candidate;
    ADD_FAILURE() << "no stream " << key;
    static const StreamSpec none;
    return none;
}

Names numbered (const std::string& prefix, int count, int first = 1)
{
    Names names;
    for (int k = first; k < first + count; ++k)
        names.push_back (prefix + std::to_string (k));
    return names;
}
}

TEST (Aes67Streams, TheShippedSixOneLayoutGivesSevenChannelsPerZone)
{
    const auto streams = resolveTxStreams (shippedTruth(), sixOneSpatial(), "a3nuc1");
    for (const std::string zone : { "main", "booth" })
    {
        const auto& spec = stream (streams, "core-" + zone);
        Names names { "SUB" };
        for (const auto& top : numbered ("T", 6))
            names.push_back (top);
        Names ports { zone + "_sub" };
        for (const auto& top : numbered (zone + "_top_", 6))
            ports.push_back (top);
        EXPECT_EQ (spec.channelNames, names) << zone;
        EXPECT_EQ (spec.enginePorts, ports) << zone;
    }

    const auto fourOne = resolveTxStreams (shippedTruth(), shippedSpatial(), "a3nuc1");
    EXPECT_EQ (stream (fourOne, "core-main").channelNames, (Names { "SUB", "T1", "T2", "T3", "T4" }));
}

TEST (Aes67Streams, ALayoutWithoutASubHasNChannels)
{
    const auto streams = resolveTxStreams (shippedTruth(), spatialWith (layout (0, 4), layout (0, 4)), "a3nuc1");
    const auto& main = stream (streams, "core-main");
    EXPECT_EQ (main.channelNames, numbered ("T", 4));
    EXPECT_EQ (main.enginePorts, numbered ("main_top_", 4));
}

TEST (Aes67Streams, OneSpeakerAndASubIsTwoChannels)
{
    const auto streams = resolveTxStreams (shippedTruth(), spatialWith (layout (1, 1), layout (1, 1)), "a3nuc1");
    const auto& booth = stream (streams, "core-booth");
    EXPECT_EQ (booth.channelNames, (Names { "SUB", "T1" }));
    EXPECT_EQ (booth.enginePorts, (Names { "booth_sub", "booth_top_1" }));
}

TEST (Aes67Streams, RecAndPhonesAreStereoPairs)
{
    const auto streams = resolveTxStreams (shippedTruth(), shippedSpatial(), "a3nuc1");
    const auto& rec = stream (streams, "core-rec");
    const auto& phones = stream (streams, "core-phones");
    EXPECT_EQ (rec.channelNames, (Names { "L", "R" }));
    EXPECT_EQ (rec.enginePorts, (Names { "rec_L", "rec_R" }));
    EXPECT_EQ (phones.channelNames, (Names { "L", "R" }));
    EXPECT_EQ (phones.enginePorts, (Names { "phones_L", "phones_R" }));
}

TEST (Aes67Streams, GroupsAndNamesAreTheTablesAndTheLegacyStaysFirst)
{
    const auto streams = resolveTxStreams (shippedTruth(), shippedSpatial(), "a3nuc1");
    ASSERT_EQ (streams.size(), 5u);

    const auto& legacy = streams[0];
    EXPECT_EQ (legacy.key, "legacy-out");
    EXPECT_TRUE (legacy.legacy);
    EXPECT_EQ (legacy.session, "AES67 a3nuc1 out");
    EXPECT_EQ (legacy.group, "239.69.150.243");
    EXPECT_EQ (legacy.port, 5004);
    EXPECT_DOUBLE_EQ (legacy.ptimeMs, 0.5);
    EXPECT_EQ (legacy.channelNames, numbered ("CH", 12));
    EXPECT_TRUE (legacy.enginePorts.empty());

    const Names keys { "core-main", "core-booth", "core-rec", "core-phones" };
    const Names sessions { "AES67 Core MAIN", "AES67 Core BOOTH", "AES67 Core REC", "AES67 Core PHONES" };
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const auto& spec = streams[i + 1];
        EXPECT_EQ (spec.key, keys[i]);
        EXPECT_EQ (spec.session, sessions[i]);
        EXPECT_EQ (spec.group, "239.69.200." + std::to_string (i + 1));
        EXPECT_EQ (spec.port, 5004);
        EXPECT_DOUBLE_EQ (spec.ptimeMs, 0.5);
        EXPECT_FALSE (spec.legacy);
    }
}

TEST (Aes67Streams, NoTableMeansTheLegacyStreamOnly)
{
    auto data = shippedTruth().data();
    data.getDynamicObject()->removeProperty ("aes67_streams");
    const Truth withoutTable (data);
    EXPECT_EQ (withoutTable.streamTable().size(), 0);

    const auto streams = resolveTxStreams (withoutTable, shippedSpatial(), "a3nuc1");
    ASSERT_EQ (streams.size(), 1u);
    EXPECT_EQ (streams[0].key, "legacy-out");
    EXPECT_EQ (streams[0].session, "AES67 a3nuc1 out");
    EXPECT_EQ (streams[0].group, "239.69.150.243");
    EXPECT_EQ (streams[0].channelNames, numbered ("CH", 12));
}

TEST (Aes67Streams, TheLegacyStreamAloneNeedsNoLayout)
{
    const auto legacy = legacyTxStreams (shippedTruth(), "a3nuc1");
    ASSERT_EQ (legacy.size(), 1u);
    EXPECT_EQ (streamsSignature (legacy), streamsSignature ({ resolveTxStreams (shippedTruth(), sixOneSpatial(), "a3nuc1").front() }));
    EXPECT_TRUE (legacy[0].legacy);
}

TEST (Aes67Streams, TheSignatureChangesWithTheSpeakerCountAndNotOtherwise)
{
    const auto spatial = shippedSpatial();
    const auto once = streamsSignature (resolveTxStreams (shippedTruth(), spatial, "a3nuc1"));
    const auto twice = streamsSignature (resolveTxStreams (shippedTruth(), spatial, "a3nuc1"));
    EXPECT_FALSE (once.empty());
    EXPECT_EQ (once, twice);

    auto grown = spatial;
    grown.main.layout.topChannels.push_back (grown.main.layout.numOutputs() + 1);
    EXPECT_NE (streamsSignature (resolveTxStreams (shippedTruth(), grown, "a3nuc1")), once);
}

TEST (Aes67Streams, AHostnameWithSpacesGivesALegacySessionThatEndsInOut)
{
    const auto streams = resolveTxStreams (shippedTruth(), shippedSpatial(), "My Box");
    ASSERT_FALSE (streams.empty());
    EXPECT_EQ (streams[0].session, "AES67 My Box out");
    EXPECT_TRUE (std::regex_search (streams[0].session, std::regex (" out$")));
}

// F2 (2026-11-06): one packet a frame (1472 bytes of UDP payload under a 1500-byte MTU). A layout above it refuses
// just that stream and says so; the others are still sent.
TEST (Aes67Streams, AZoneAboveThePacketLimitIsDroppedAloneAndSaid)
{
    std::vector<std::string> problems;
    const auto streams = resolveTxStreams (shippedTruth(), spatialWith (layout (1, 22), layout (1, 4)), "a3nuc1", &problems);
    Names keys;
    for (const auto& spec : streams)
        keys.push_back (spec.key);
    EXPECT_EQ (keys, (Names { "legacy-out", "core-booth", "core-rec", "core-phones" }));
    ASSERT_EQ (problems.size(), 1u);
    EXPECT_NE (problems[0].find ("'core-main'"), std::string::npos) << problems[0];
    EXPECT_NE (problems[0].find ("1472"), std::string::npos) << problems[0];
}

TEST (Aes67Streams, TwentyTwoChannelsStillFitOnePacket)
{
    std::vector<std::string> problems;
    const auto streams = resolveTxStreams (shippedTruth(), spatialWith (layout (1, 21), layout (1, 4)), "a3nuc1", &problems);
    EXPECT_EQ (stream (streams, "core-main").channelNames.size(), 22u);
    EXPECT_TRUE (problems.empty()) << problems.front();
}

namespace
{
// The shipped truth with one table row changed by `change`.
Truth withRowChanged (const std::string& key, const std::function<void (juce::DynamicObject&)>& change)
{
    auto data = shippedTruth().data();
    for (auto& row : *data["aes67_streams"].getArray())
        if (row["key"].toString().toStdString() == key)
            change (*row.getDynamicObject());
    return Truth (data);
}

std::function<void (juce::DynamicObject&)> setting (const char* property, juce::var value)
{
    return [property, value] (juce::DynamicObject& row) { row.setProperty (property, value); };
}

void expectTheBuiltInLegacyStream (const StreamSpec& legacy)
{
    EXPECT_EQ (legacy.key, "legacy-out");
    EXPECT_TRUE (legacy.legacy);
    EXPECT_EQ (legacy.session, "AES67 a3nuc1 out");
    EXPECT_EQ (legacy.group, "239.69.150.243");
    EXPECT_EQ (legacy.port, 5004);
    EXPECT_DOUBLE_EQ (legacy.ptimeMs, 0.5);
    EXPECT_EQ (legacy.channelNames, numbered ("CH", 12));
}
}

// F3 (2026-11-06): the table is validated. A legacy row that is invalid or does not resolve gives way to the built-in
// one (the stream every receiver of the rig already knows) with a line saying so; never a Core without its sender.
TEST (Aes67Streams, AnInvalidOrUnresolvableLegacyRowFallsBackToTheBuiltInOne)
{
    for (const auto& [why, truth] : std::vector<std::pair<std::string, Truth>> {
             { "port 0", withRowChanged ("legacy-out", setting ("port", 0)) },
             { "port 70000", withRowChanged ("legacy-out", setting ("port", 70000)) },
             { "ptime 0", withRowChanged ("legacy-out", setting ("ptime", 0)) },
             { "no channels", withRowChanged ("legacy-out", setting ("channels", 0)) },
             { "no group", withRowChanged ("legacy-out", [] (juce::DynamicObject& row) { row.removeProperty ("group"); }) },
             { "an unknown host", withRowChanged ("legacy-out", setting ("group", "no-such-host")) },
             { "an unknown source", withRowChanged ("legacy-out", setting ("source", "zone:nowhere")) } })
    {
        std::vector<std::string> problems;
        const auto streams = resolveTxStreams (truth, shippedSpatial(), "a3nuc1", &problems);
        ASSERT_EQ (streams.size(), 5u) << why;
        expectTheBuiltInLegacyStream (streams[0]);
        ASSERT_EQ (problems.size(), 1u) << why;
        EXPECT_NE (problems[0].find ("'legacy-out'"), std::string::npos) << why << ": " << problems[0];

        std::vector<std::string> alone;
        const auto legacy = legacyTxStreams (truth, "a3nuc1", &alone);
        ASSERT_EQ (legacy.size(), 1u) << why;
        expectTheBuiltInLegacyStream (legacy[0]);
        EXPECT_EQ (alone, problems) << why;
    }
}

TEST (Aes67Streams, AnInvalidCoreRowIsDroppedAndSaid)
{
    for (const auto& [why, key, truth] : std::vector<std::tuple<std::string, std::string, Truth>> {
             { "port 0", "core-rec", withRowChanged ("core-rec", setting ("port", 0)) },
             { "port 65536", "core-phones", withRowChanged ("core-phones", setting ("port", 65536)) },
             { "ptime -1", "core-booth", withRowChanged ("core-booth", setting ("ptime", -1)) },
             { "no group", "core-main", withRowChanged ("core-main", [] (juce::DynamicObject& row) { row.removeProperty ("group"); }) },
             { "a fixed row without channels", "core-rec",
               withRowChanged ("core-rec", [] (juce::DynamicObject& row) { row.setProperty ("source", "fixed"); row.setProperty ("channels", 0); }) } })
    {
        std::vector<std::string> problems;
        const auto streams = resolveTxStreams (truth, shippedSpatial(), "a3nuc1", &problems);
        EXPECT_EQ (streams.size(), 4u) << why;
        for (const auto& spec : streams)
            EXPECT_NE (spec.key, key) << why;
        ASSERT_EQ (problems.size(), 1u) << why;
        EXPECT_NE (problems[0].find ("'" + key + "'"), std::string::npos) << why << ": " << problems[0];
    }
}
