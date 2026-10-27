// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of the module cases of a3-core's tools/tests/test_core_announce.py (B0 f3345dc7feeb): Core
// says where the truth is, every 2 s, by UDP broadcast. The source-text cases (CoreAnnounces,
// CoreRendersForItsNeighbours) belong to the tasks that wire the program. The packet and the
// broadcast table are recorded by tools/parity/record_announce.py.
#include <gtest/gtest.h>

#include "Announce.h"
#include "B0Truth.h"
#include "Json.h"

using namespace a3core;

namespace
{
juce::var networkOf (const juce::String& address)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("address", address);
    return juce::var (object);
}

juce::var recorded()
{
    static const juce::var loaded = [] {
        juce::String error;
        auto value = parseJson (juce::File (A3_CORE_TEST_DATA "/parity/announce.json").loadFileAsString(), error);
        EXPECT_EQ (error, juce::String());
        return value;
    }();
    return loaded;
}

std::vector<uint8_t> hexBytes (const juce::String& hex)
{
    std::vector<uint8_t> out;
    for (int i = 0; i + 1 < hex.length(); i += 2)
        out.push_back ((uint8_t) hex.substring (i, i + 2).getHexValue32());
    return out;
}
}

// WhereTheBroadcastGoes

TEST (Announce, TheSubnetsBroadcastAddress)
{
    EXPECT_EQ (broadcastAddress (networkOf ("192.168.8.10/24")), "192.168.8.255");
}

TEST (Announce, ASingleHostFallsBackToLimitedBroadcast)
{
    EXPECT_EQ (broadcastAddress (networkOf ("192.168.8.10/32")), "255.255.255.255");
    EXPECT_EQ (broadcastAddress (networkOf ("192.168.8.10")), "255.255.255.255");
}

TEST (Announce, AMissingOrBrokenAddressFallsBackToo)
{
    EXPECT_EQ (broadcastAddress (juce::var (new juce::DynamicObject())), "255.255.255.255");
    EXPECT_EQ (broadcastAddress (networkOf ("nonsense")), "255.255.255.255");
}

// What Python's ipaddress made of forty kinds of address (masks, host masks, leading zeros ...).
TEST (Announce, EveryAddressTheRecorderTriedAnswersAsPython)
{
    const auto table = recorded()["broadcast"];
    ASSERT_GT (table.size(), 20);
    for (const auto& row : *table.getArray())
        EXPECT_EQ (broadcastAddress (networkOf (row["address"].toString())), row["result"].toString().toStdString())
            << row["address"].toString();
}

// WhatIsSaid

TEST (Announce, TheUrlIsCoresWindow)
{
    const auto& truth = b0::truth();
    EXPECT_EQ (truthUrl (truth), "http://" + truth.host ("core") + ":" + std::to_string (truth.port ("core", "web")) + "/api/truth");
}

TEST (Announce, TheMessageSaysUrlFingerprintAndBootId)
{
    const auto& truth = b0::truth();
    const auto packet = announcement (truth, "abc123");
    const auto message = decodeMessage (packet.data(), packet.size());
    ASSERT_TRUE (message.has_value());
    EXPECT_EQ (message->address, truth.address ("core.here"));
    EXPECT_EQ (message->args, (std::vector<OscArg> { pyStr (truthUrl (truth)), pyStr (truth.fingerprint()), pyStr ("abc123") }));
}

TEST (Announce, TheTruthDefinesThreeStrings)
{
    EXPECT_EQ (b0::truth().data()["addresses"]["core.here"]["args"].toString().toStdString(), "sss");
}

TEST (Announce, EveryStartHasItsOwnBootId)
{
    const auto first = newBootId(), second = newBootId();
    EXPECT_FALSE (first.empty());
    EXPECT_NE (first, second);
    EXPECT_EQ (first.size(), 16u);   // secrets.token_hex(8)
}

TEST (Announce, ABootIdComesFromWhoeverIsInjected)
{
    int calls = 0;
    const BootIdSource source = [&] { return "boot-" + std::to_string (++calls); };
    EXPECT_EQ (source(), "boot-1");
    EXPECT_EQ (source(), "boot-2");
}

TEST (Announce, ItIsEverySecondsTwo)
{
    EXPECT_EQ (everySeconds, 2.0);
}

// The point of the module: a device compares these bytes with the ones Python's Core sent.
TEST (Announce, PacketIsByteEqualToPython)
{
    const auto data = recorded();
    EXPECT_EQ (truthUrl (b0::truth()), data["url"].toString().toStdString());
    EXPECT_EQ (b0::truth().fingerprint(), data["fingerprint"].toString().toStdString());
    EXPECT_EQ (announcement (b0::truth(), data["boot_id"].toString().toStdString()), hexBytes (data["packet_hex"].toString()));
}

// TheLoop

TEST (Announce, AFailedSendIsReportedAndTheLoopGoesOn)
{
    std::vector<std::string> reports, sent;
    const Endpoint target { "255.255.255.255", 7790 };
    const std::vector<uint8_t> packet { 'x' };

    const auto sendTo = [&] (const Endpoint&, const std::vector<uint8_t>& bytes) {
        if (sent.empty())
        {
            sent.push_back ("fail");
            throw OsError ("network unreachable");
        }
        sent.push_back (std::string (bytes.begin(), bytes.end()));
    };

    std::vector<bool> passes { true, true, false };
    size_t pass = 0;
    std::vector<double> slept;
    announce (sendTo, target, packet, 2.0, [&] (double seconds) { slept.push_back (seconds); },
              [&] { return passes.at (pass++); }, [&] (const std::string& line) { reports.push_back (line); });

    EXPECT_EQ (sent, (std::vector<std::string> { "fail", "x" }));
    ASSERT_EQ (reports.size(), 1u);
    EXPECT_EQ (reports[0], "announce to ('255.255.255.255', 7790): network unreachable");
    EXPECT_EQ (slept, (std::vector<double> { 2.0, 2.0 }));
}
