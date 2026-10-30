// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of the monitor half of a3-core's tools/tests/test_core_web.py and the API case of
// test_core_devices.py (B0 f3345dc7feeb): the JSON a3-core serves for people, and the truth that
// StemDeck and the desks fetch. Plus what tools/parity/record_api.py recorded from B0's window over
// real HTTP for one scripted state; at the end, B1's bench, status and list rows (bench.json) and Core's own
// rig. The HTML page is gone. Every test talks real HTTP to an ephemeral port on loopback.
#include <atomic>
#include <sys/ioctl.h>

#include <chrono>
#include <fstream>
#include <map>
#include <thread>

#include <gtest/gtest.h>

#include "B0Truth.h"
#include "CoreApi.h"
#include "CoreRig.h"
#include "HttpClient.h"
#include "HttpServer.h"
#include "StateRecorded.h"
#include "TrafficRecorded.h"
#include "Version.h"

using namespace a3core;
using namespace recordedtraffic;
using corerig::CoreRig;
namespace hc = httpclient;

namespace
{
struct TestClock : Clock
{
    std::atomic<double> mono { 100.0 }, wallTime { 0 };
    double monotonic() override { return mono; }
    double wall() override { return wallTime; }
};

juce::var parsedBody (const std::string& body)
{
    juce::String error;
    auto value = parseJson (juce::String::fromUTF8 (body.data(), static_cast<int> (body.size())), error);
    EXPECT_EQ (error, juce::String()) << body.substr (0, 200);
    return value;
}

juce::File shippedRegister() { return juce::File (A3_CORE_TEST_DATA "/parity/traffic/osc-register.json"); }

// A window as Core would open it: the monitor routes on a server on an ephemeral loopback port.
struct Window
{
    Traffic traffic;
    Devices devices { "abc" };
    TestClock clock;
    juce::File registerFile = shippedRegister();
    bool withTruth = true, withDevices = true;
    int streamHz = 4;
    std::vector<std::string> said;
    std::unique_ptr<HttpServer> server;

    bool open (const std::string& bind = loopbackAddress() + ":0")
    {
        Routes routes;
        addMonitorRoutes (routes, { withTruth ? &b0::truth() : nullptr, traffic, withDevices ? &devices : nullptr, registerFile, clock, streamHz });
        server = std::make_unique<HttpServer> (bind, std::move (routes));
        return server->start ([this] (const std::string& line, bool) { said.push_back (line); });
    }

    ~Window()
    {
        if (server)
            server->stop();
    }

    int port() const { return server->port(); }
    hc::Reply get (const std::string& path) const { return hc::get (port(), path); }
    juce::var json (const std::string& path) const
    {
        const auto reply = get (path);
        EXPECT_EQ (reply.status, 200) << path;
        return parsedBody (reply.body);
    }
};

TrafficRow rowOf (Direction direction, const std::string& address, const std::string& peer, long long count, long long bytes,
                  double lastSeen = 0)
{
    TrafficRow row;
    row.direction = direction;
    row.address = address;
    row.count = count;
    row.bytes = bytes;
    row.lastValue = pyFloat (1.0);
    row.lastType = "float";
    row.lastSeen = lastSeen;
    row.peer = peer;
    return row;
}

TrafficSnapshot snapshotOf (double at, std::vector<TrafficRow> rows)
{
    TrafficSnapshot snapshot;
    snapshot.at = at;
    snapshot.rows = std::move (rows);
    return snapshot;
}

double number (const juce::var& value) { return static_cast<double> (value); }

juce::var apiFixture()
{
    static const juce::var recorded = [] {
        juce::String error;
        auto value = parseJson (juce::File (A3_CORE_TEST_DATA "/parity/api/api.json").loadFileAsString(), error);
        EXPECT_EQ (error, juce::String());
        return value;
    }();
    return recorded;
}

// The recorded state, replayed into a window.
void replayInto (Window& window)
{
    play (window.traffic, apiFixture()["first_ops"]);
    EXPECT_EQ (window.devices.ownHash, apiFixture()["own_hash"].toString().toStdString());
    for (const auto& heard : *apiFixture()["devices"].getArray())
        window.devices.heard (heard[0].toString().toStdString(), heard[1].toString().toStdString(), static_cast<double> (heard[2]));
}
}

// The rate comes from two snapshots.

TEST (CoreApi, TenMessagesInTwoSecondsIsFiveASecond)
{
    const auto first = snapshotOf (100.0, { rowOf (Direction::in, "/a", "motion", 5, 0, 100.0) });
    const auto second = snapshotOf (102.0, { rowOf (Direction::in, "/a", "motion", 15, 0, 102.0) });
    EXPECT_DOUBLE_EQ (number (snapshotAsJson (second, &first)["rows"][0]["rate"]), 5.0);
}

TEST (CoreApi, WithoutAPreviousSnapshotThereIsNoRate)
{
    const auto snapshot = snapshotOf (100.0, { rowOf (Direction::in, "/a", "motion", 5, 0, 100.0) });
    EXPECT_TRUE (snapshotAsJson (snapshot, nullptr)["rows"][0]["rate"].isVoid());
}

TEST (CoreApi, ARowThatIsNewSinceTheLastLookHasNoRate)
{
    const auto first = snapshotOf (100.0, {});
    const auto second = snapshotOf (101.0, { rowOf (Direction::in, "/b", "motion", 3, 0, 101.0) });
    EXPECT_TRUE (snapshotAsJson (second, &first)["rows"][0]["rate"].isVoid());
}

TEST (CoreApi, TwoSnapshotsAtTheSameInstantDoNotDivideByZero)
{
    const auto same = snapshotOf (100.0, { rowOf (Direction::in, "/a", "motion", 5, 0, 100.0) });
    EXPECT_TRUE (snapshotAsJson (same, &same)["rows"][0]["rate"].isVoid());
}

TEST (CoreApi, TheTwoDirectionsOfOneAddressGetTheirOwnRates)
{
    const auto first = snapshotOf (100.0, { rowOf (Direction::in, "/a", "motion", 0, 0, 100.0), rowOf (Direction::out, "/a", "reaper", 0, 0, 100.0) });
    const auto second = snapshotOf (101.0, { rowOf (Direction::in, "/a", "motion", 4, 0, 101.0), rowOf (Direction::out, "/a", "reaper", 1, 0, 101.0) });
    const auto rows = snapshotAsJson (second, &first)["rows"];
    EXPECT_NEAR (number (rows[0]["rate"]), 4.0, 1e-9);
    EXPECT_NEAR (number (rows[1]["rate"]), 1.0, 1e-9);
}

// The totals are per peer and direction.

namespace
{
struct Totals
{
    TrafficSnapshot first = snapshotOf (100.0, { rowOf (Direction::in, "/a", "motion", 5, 60), rowOf (Direction::in, "/d", "motion", 2, 24),
                                                 rowOf (Direction::out, "/c", "reaper", 10, 200) });
    TrafficSnapshot second = snapshotOf (102.0, { rowOf (Direction::in, "/a", "motion", 15, 180), rowOf (Direction::in, "/d", "motion", 6, 72),
                                                  rowOf (Direction::out, "/c", "reaper", 30, 600) });

    // The line of a peer and direction, or a void var.
    juce::var line (const std::string& peer, const std::string& direction)
    {
        const auto totals = snapshotAsJson (second, &first)["totals"];
        for (const auto& candidate : *totals.getArray())
            if (candidate["peer"].toString() == peer.c_str() && candidate["direction"].toString() == direction.c_str())
                return candidate;
        return {};
    }
};
}

TEST (CoreApi, TheRowsOfOnePeerAndDirectionAddUp)
{
    Totals totals;
    const auto motion = totals.line ("motion", "in");
    EXPECT_NEAR (number (motion["rate"]), 7.0, 1e-9);     // (10 + 4) / 2 s
    EXPECT_NEAR (number (motion["kbit"]), 0.672, 1e-9);   // 168 B / 2 s * 8 / 1000
}

TEST (CoreApi, EachPeerAndDirectionIsItsOwnLine)
{
    Totals totals;
    EXPECT_EQ (snapshotAsJson (totals.second, &totals.first)["totals"].size(), 2);
    EXPECT_NEAR (number (totals.line ("reaper", "out")["rate"]), 10.0, 1e-9);
    EXPECT_NEAR (number (totals.line ("reaper", "out")["kbit"]), 1.6, 1e-9);
    EXPECT_TRUE (totals.line ("motion", "out").isVoid());
}

TEST (CoreApi, ThereIsAnOverallSumPerDirection)
{
    Totals totals;
    const auto overall = snapshotAsJson (totals.second, &totals.first)["overall"];
    EXPECT_NEAR (number (overall["in"]["rate"]), 7.0, 1e-9);
    EXPECT_NEAR (number (overall["out"]["kbit"]), 1.6, 1e-9);
}

TEST (CoreApi, WithoutAPreviousSnapshotThereAreNoTotalsYet)
{
    Totals totals;
    // Not zero: zero would say that nothing is arriving.
    const auto out = snapshotAsJson (totals.second, nullptr);
    EXPECT_TRUE (out["totals"].isVoid());
    EXPECT_TRUE (out["overall"].isVoid());
}

TEST (CoreApi, ARowNewSinceTheLastLookIsLeftOut)
{
    Totals totals;
    totals.second.rows.push_back (rowOf (Direction::in, "/new", "mixer", 50, 600));
    EXPECT_TRUE (totals.line ("mixer", "in").isVoid());
}

TEST (CoreApi, ARowWithoutAByteCountCountsItsMessages)
{
    Totals totals;
    // A snapshot from before bytes were counted, or a restored row.
    for (auto* snapshot : { &totals.first, &totals.second })
        for (auto& row : snapshot->rows)
            row.bytes = 0;
    const auto motion = totals.line ("motion", "in");
    EXPECT_NEAR (number (motion["rate"]), 7.0, 1e-9);
    EXPECT_NEAR (number (motion["kbit"]), 0.0, 1e-9);
}

// What kind of history this is.

TEST (CoreApi, WithNoPreviousSnapshotTheHistoryIsReportedFull)
{
    EXPECT_TRUE (static_cast<bool> (snapshotAsJson (snapshotOf (100.0, {}), nullptr)["full"]));
}

TEST (CoreApi, WithAPreviousSnapshotTheHistoryIsReportedNotFull)
{
    const auto previous = snapshotOf (99.0, {});
    const auto full = snapshotAsJson (snapshotOf (100.0, {}), &previous)["full"];
    EXPECT_TRUE (full.isBool());
    EXPECT_FALSE (static_cast<bool> (full));
}

// Everything survives the dump.

TEST (CoreApi, ASnapshotOfRealTrafficSerialises)
{
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/pfl", 1.0, "motion");
    const auto text = pyStr ("1");
    traffic.seen (Direction::in, "/channel/0/pfl", &text, "mixer", tick());
    seenFloat (traffic, Direction::out, "/track/4/mute", 0.0, "reaper");
    unknownOf (traffic, "/track/3/name", pyFloat (0.0));

    const auto dumped = canonicalBytes (snapshotAsJson (traffic.snapshot (std::nullopt, tick()), nullptr));
    EXPECT_NE (dumped.find ("/channel/0/pfl"), std::string::npos);
}

TEST (CoreApi, AValueJsonCannotHoldBecomesItsRepr)
{
    // OSC can carry blobs. The window must not fall over on one.
    Traffic traffic;
    const OscArg blob { 'b', std::vector<uint8_t> { 0x00, 0x01 } };
    traffic.seen (Direction::in, "/blob", &blob, "motion", tick());
    const auto row = snapshotAsJson (traffic.snapshot (std::nullopt, tick()), nullptr)["rows"][0];
    EXPECT_TRUE (row["last_value"].isString());
    EXPECT_EQ (row["last_type"].toString(), "bytes");
}

TEST (CoreApi, ANonFiniteFloatSurvivesAStrictParser)
{
    // Over the wire: a bare NaN/Infinity is not JSON, and a browser's JSON.parse throws on it.
    Window window;
    seenFloat (window.traffic, Direction::in, "/a", std::numeric_limits<double>::quiet_NaN(), "motion");
    seenFloat (window.traffic, Direction::in, "/b", std::numeric_limits<double>::infinity(), "motion");
    seenFloat (window.traffic, Direction::in, "/c", -std::numeric_limits<double>::infinity(), "motion");
    ASSERT_TRUE (window.open());

    const auto reply = window.get ("/api/traffic");
    EXPECT_EQ (reply.body.find ("NaN"), std::string::npos);
    EXPECT_EQ (reply.body.find ("Infinity"), std::string::npos);
    const auto rows = parsedBody (reply.body)["rows"];
    std::map<std::string, std::string> values;
    for (const auto& row : *rows.getArray())
        values[row["address"].toString().toStdString()] = row["last_value"].toString().toStdString();
    EXPECT_EQ (values["/a"], "nan");
    EXPECT_EQ (values["/b"], "inf");
    EXPECT_EQ (values["/c"], "-inf");
}

TEST (CoreApi, ANonFiniteValueSurvivesHereToo)
{
    Window window;
    unknownOf (window.traffic, "/track/3/pan", pyFloat (std::numeric_limits<double>::quiet_NaN()));
    ASSERT_TRUE (window.open());
    const auto reply = window.get ("/api/unknown");
    EXPECT_EQ (reply.body.find ("NaN"), std::string::npos);
    EXPECT_EQ (parsedBody (reply.body)["rows"][0]["last_value"].toString(), "nan");
}

// The truth is served.

TEST (CoreApi, TheTruthIsServedWithItsFingerprint)
{
    Window window;
    ASSERT_TRUE (window.open());
    const auto reply = window.get ("/api/truth");
    EXPECT_EQ (reply.status, 200);
    EXPECT_EQ (reply.body, b0::truth().canonical());
    EXPECT_EQ (reply.headers.at ("x-a3-truth"), b0::truth().fingerprint());
    EXPECT_EQ (reply.headers.at ("content-type"), "application/json");
}

TEST (CoreApi, WithoutATruthThereIsNoneToServe)
{
    Window window;
    window.withTruth = false;
    ASSERT_TRUE (window.open());
    EXPECT_EQ (window.get ("/api/truth").status, 404);
}

TEST (CoreApi, TruthIsByteEqualWithItsHeader)
{
    // What StemDeck, the desk, Motion and the pro-DJ app hash: the bytes B0's window answered with for
    // the shipped truth, and the fingerprint of those bytes in the header.
    const auto& recorded = apiFixture()["responses"]["truth"];
    juce::MemoryOutputStream bytes;
    ASSERT_TRUE (juce::Base64::convertFromBase64 (bytes, recorded["body_b64"].toString()));
    const std::string expected (static_cast<const char*> (bytes.getData()), bytes.getDataSize());

    Window window;
    ASSERT_TRUE (window.open());
    const auto reply = window.get ("/api/truth");
    EXPECT_EQ (reply.body, expected);
    EXPECT_EQ (reply.headers.at ("x-a3-truth"), recorded["header"].toString().toStdString());
    EXPECT_EQ (reply.headers.at ("content-type"), recorded["content_type"].toString().toStdString());
    EXPECT_EQ (sha256Hex (reply.body), reply.headers.at ("x-a3-truth")) << "the header is the hash of the body";
}

TEST (CoreApi, TheTruthHasNoLineBreakOrChunkingThatMovesAByte)
{
    // A reader that hashes what it reads gets the same hash from a plain read to the length.
    Window window;
    ASSERT_TRUE (window.open());
    const auto reply = window.get ("/api/truth");
    EXPECT_EQ (reply.headers.at ("content-length"), std::to_string (b0::truth().canonical().size()));
    EXPECT_EQ (reply.headers.count ("transfer-encoding"), 0u);
}

// The server answers.

TEST (CoreApi, TheSnapshotIsJsonAndHasTheRow)
{
    Window window;
    seenFloat (window.traffic, Direction::in, "/channel/0/gain", 0.5, "motion");
    ASSERT_TRUE (window.open());
    const auto reply = window.get ("/api/traffic");
    EXPECT_EQ (reply.headers.at ("content-type"), "application/json");
    EXPECT_EQ (parsedBody (reply.body)["rows"][0]["address"].toString(), "/channel/0/gain");
}

TEST (CoreApi, AnUnknownPathIsA404AndNotACrash)
{
    Window window;
    ASSERT_TRUE (window.open());
    const auto reply = window.get ("/nope");
    EXPECT_EQ (reply.status, 404);
    EXPECT_EQ (reply.body, "no such thing here");
    EXPECT_EQ (window.get ("/api/traffic").status, 200);
}

TEST (CoreApi, TheRootIsA404ThatSaysWhereTheJsonIs)
{
    // No page any more (O1): the text names /api/... and the a3-core window.
    Window window;
    ASSERT_TRUE (window.open());
    const auto reply = window.get ("/");
    EXPECT_EQ (reply.status, 404);
    EXPECT_EQ (reply.headers.at ("content-type"), "text/plain");
    EXPECT_NE (reply.body.find ("/api/"), std::string::npos) << reply.body;
    EXPECT_NE (reply.body.find ("a3-core"), std::string::npos) << reply.body;
    EXPECT_NE (reply.body, "no such thing here");
}

TEST (CoreApi, TheRetiredRoutesAreGone)
{
    Window window;
    ASSERT_TRUE (window.open());
    EXPECT_EQ (window.get ("/index.html").status, 404);
    EXPECT_EQ (hc::post (window.port(), "/api/refresh-reaper", "").status, 404);
}

// The unknown table is fetched on request.

TEST (CoreApi, UnknownAsJsonShapesRowsLikeTheOthers)
{
    Traffic traffic;
    unknownOf (traffic, "/track/3/pan", pyFloat (0.25));
    const auto payload = unknownAsJson (traffic.unknownSnapshot (tick()));
    const auto row = payload["rows"][0];
    EXPECT_EQ (row["address"].toString(), "/track/3/pan");
    EXPECT_EQ (row["direction"].toString(), "in");
    EXPECT_TRUE (row["rate"].isVoid());
    EXPECT_TRUE (row["unknown"].isBool());
    EXPECT_TRUE (static_cast<bool> (row["unknown"]));
    EXPECT_GE (number (row["age"]), 0.0);
}

TEST (CoreApi, TheRouteAnswersWithTheTable)
{
    Window window;
    unknownOf (window.traffic, "/track/3/name", pyFloat (0.0));
    ASSERT_TRUE (window.open());
    EXPECT_EQ (window.json ("/api/unknown")["rows"][0]["address"].toString(), "/track/3/name");
}

TEST (CoreApi, TheRouteIsNotOnTheStream)
{
    // /api/traffic stays small no matter how much is unknown.
    Window window;
    for (int i = 0; i < 5000; ++i)
        unknownOf (window.traffic, "/track/" + std::to_string (i) + "/name", pyFloat (0.0));
    ASSERT_TRUE (window.open());
    EXPECT_LT (window.get ("/api/traffic").body.size(), 8u * 1024);
    EXPECT_GT (window.get ("/api/unknown").body.size(), 100u * 1024);
}

TEST (CoreApi, AsJsonReportsTheSizeAndDropsTheContents)
{
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "motion");
    for (int i = 0; i < 2000; ++i)
        unknownOf (traffic, "/track/" + std::to_string (i) + "/name", pyFloat (0.0));
    const auto payload = snapshotAsJson (traffic.snapshot (std::nullopt, tick()), nullptr);
    EXPECT_FALSE (payload.hasProperty ("unhandled"));
    EXPECT_EQ (static_cast<int> (payload["unknown_addresses"]), 2000);
    EXPECT_EQ (payload["rows"].size(), 1);
    EXPECT_LT (canonicalBytes (payload).size(), 8u * 1024);
}

TEST (CoreApi, BothEvictionCountersAreInTheStreamedPayload)
{
    // Neither cap may lose rows silently: both counters have to survive into what the stream sends.
    Traffic traffic (Traffic::historyCap, 3, 5);
    for (int i = 0; i < 9; ++i)
        unknownOf (traffic, "/track/" + std::to_string (i), pyFloat (0.0));
    for (int i = 0; i < 7; ++i)
        seenFloat (traffic, Direction::in, "/channel/" + std::to_string (i) + "/gain", 0.0, "motion");
    const auto payload = snapshotAsJson (traffic.snapshot (std::nullopt, tick()), nullptr);
    EXPECT_EQ (static_cast<int> (payload["evicted"]["unknown"]), 4);
    EXPECT_EQ (static_cast<int> (payload["evicted"]["rows"]), 4);
}

// The register route.

namespace
{
struct RegisterWindow
{
    recorded::TempDir temp;
    Window window;

    RegisterWindow()
    {
        window.registerFile = temp.dir.getChildFile ("osc-register.json");
        recorded::writeBytes (window.registerFile,
                              "{\"devices\": [\"core\", \"mixer\"], \"directions\": [\"in\"], \"entries\": ["
                              "{\"address\": \"/channel/{ch}/gain\", \"device\": \"mixer\", \"direction\": \"in\", \"source\": \"a3-mixer.py:62\", \"note\": \"\"},"
                              "{\"address\": \"/channel/{ch}/encbtn\", \"device\": \"mixer\", \"direction\": \"in\", \"source\": \"a3-core.py:577\", \"note\": \"\"}]}");
    }

    juce::var fetched()
    {
        EXPECT_TRUE (window.open());
        return window.json ("/api/register");
    }

    static juce::var entryEnding (const juce::var& payload, const juce::String& ending)
    {
        for (const auto& entry : *payload["entries"].getArray())
            if (entry["address"].toString().endsWith (ending))
                return entry;
        return {};
    }
};
}

TEST (CoreApi, TheRouteAnswersWithTheCatalogue)
{
    RegisterWindow registered;
    const auto payload = registered.fetched();
    EXPECT_EQ (payload["entries"].size(), 2);
    EXPECT_EQ (payload["devices"][0].toString(), "core");
    EXPECT_EQ (payload["devices"][1].toString(), "mixer");
}

TEST (CoreApi, AnAddressThatArrivedIsMarkedAlive)
{
    RegisterWindow registered;
    seenFloat (registered.window.traffic, Direction::in, "/channel/0/gain", 0.5, "mixer");
    const auto payload = registered.fetched();
    EXPECT_TRUE (static_cast<bool> (RegisterWindow::entryEnding (payload, "gain")["seen"]));
    EXPECT_FALSE (static_cast<bool> (RegisterWindow::entryEnding (payload, "encbtn")["seen"]));
    EXPECT_EQ (static_cast<int> (payload["unseen"]), 1);
}

TEST (CoreApi, WhatCoreCouldNotRouteCountsAsArrived)
{
    RegisterWindow registered;
    // REAPER's feedback fills the unknown table and REAPER's patterns are most of the register; a
    // catalogue that ignored it would call every one of them dead.
    unknownOf (registered.window.traffic, "/channel/3/gain", pyFloat (0.5));
    EXPECT_TRUE (static_cast<bool> (RegisterWindow::entryEnding (registered.fetched(), "gain")["seen"]));
}

TEST (CoreApi, TheHistoryDoesNotRideAlong)
{
    RegisterWindow registered;
    for (int i = 0; i < 2000; ++i)
        seenFloat (registered.window.traffic, Direction::in, "/channel/0/gain", 0.5, "mixer");
    EXPECT_FALSE (registered.fetched().hasProperty ("history"));
}

TEST (CoreApi, ARegisterThatCannotBeReadIsSaidRatherThanThrown)
{
    RegisterWindow registered;
    registered.window.registerFile.deleteFile();
    const auto payload = registered.fetched();
    EXPECT_EQ (payload["entries"].size(), 0);
    EXPECT_TRUE (pythonTruthy (payload["problem"]));
}

// The devices.

TEST (CoreApi, TheApiSaysWhoseTruthDiffers)
{
    Window window;
    window.devices.heard ("mixer", "old", 0.0);
    ASSERT_TRUE (window.open());
    const auto payload = window.json ("/api/devices");
    EXPECT_EQ (payload["own"].toString(), "abc");
    ASSERT_EQ (payload["rows"].size(), 1);
    EXPECT_EQ (payload["rows"][0]["device"].toString(), "mixer");
    EXPECT_FALSE (static_cast<bool> (payload["rows"][0]["matches"]));
}

TEST (CoreApi, WithoutADevicesListThereAreNone)
{
    Window window;
    window.withDevices = false;
    ASSERT_TRUE (window.open());
    const auto payload = window.json ("/api/devices");
    EXPECT_TRUE (payload["own"].isVoid());
    EXPECT_EQ (payload["rows"].size(), 0);
}

TEST (CoreApi, TheDefaultIsTheTruthsListener)
{
    EXPECT_EQ (defaultBind (b0::truth()), "0.0.0.0:9080");
}

// A bad bind does not take Core down.

TEST (CoreApi, ABindWithNoColonReturnsFalseAndBindsNothing)
{
    // 39080 and not the shipped default: that is what a rig legitimately listens on.
    Window window;
    EXPECT_FALSE (window.open ("39080"));
    EXPECT_FALSE (hc::connects (39080));
    ASSERT_EQ (window.said.size(), 1u);
    EXPECT_EQ (window.said[0].rfind ("the window could not open on 39080: ", 0), 0u);
}

TEST (CoreApi, AColonWithNoHostIsRefusedToo)
{
    // The other way to end up binding every interface: an empty host is INADDR_ANY.
    Window window;
    EXPECT_FALSE (window.open (":39080"));
    EXPECT_FALSE (hc::connects (39080));
}

TEST (CoreApi, ANonNumericPortReturnsFalseRatherThanRaising)
{
    Window window;
    EXPECT_FALSE (window.open (loopbackAddress() + ":not-a-port"));
}

TEST (CoreApi, APortAboveTheRangeReturnsFalseRatherThanRaising)
{
    Window window;
    EXPECT_FALSE (window.open (loopbackAddress() + ":99080"));
}

TEST (CoreApi, APortAlreadyInUseReturnsFalseRatherThanRaising)
{
    // The rule the window lives under: it never stops Core.
    Window holder;
    ASSERT_TRUE (holder.open());
    Window second;
    EXPECT_FALSE (second.open (loopbackAddress() + ":" + std::to_string (holder.port())));
    EXPECT_EQ (holder.get ("/api/traffic").status, 200);
}

// What B0's window answered for one scripted state, over real HTTP.

namespace
{
struct Recorded
{
    Window window;
    juce::var expected = apiFixture()["responses"];

    Recorded()
    {
        replayInto (window);
        window.clock.mono = static_cast<double> (apiFixture()["fetched_at"]);
        EXPECT_TRUE (window.open());
    }
};
}

TEST (CoreApi, TrafficAsPython)
{
    Recorded recorded;
    expectJsonEqual (recorded.expected["traffic"], recorded.window.json ("/api/traffic"), "/api/traffic");
}

TEST (CoreApi, UnknownAsPython)
{
    Recorded recorded;
    expectJsonEqual (recorded.expected["unknown"], recorded.window.json ("/api/unknown"), "/api/unknown");
}

TEST (CoreApi, RegisterAsPython)
{
    Recorded recorded;
    expectJsonEqual (recorded.expected["register"], recorded.window.json ("/api/register"), "/api/register");
}

TEST (CoreApi, DevicesAsPython)
{
    Recorded recorded;
    expectJsonEqual (recorded.expected["devices"], recorded.window.json ("/api/devices"), "/api/devices");
}

TEST (CoreApi, AnAbsentPathAnswersAsPythonDid)
{
    Recorded recorded;
    const auto reply = recorded.window.get ("/nope");
    EXPECT_EQ (reply.status, static_cast<int> (recorded.expected["nope"]["status"]));
    EXPECT_EQ (reply.body, recorded.expected["nope"]["body"].toString().toStdString());
    EXPECT_EQ (reply.headers.at ("content-type"), recorded.expected["nope"]["content_type"].toString().toStdString());
}

// The stream.

TEST (CoreApi, TheStreamStartsFullAndThenSendsOnlyWhatIsNew)
{
    Window window;
    window.streamHz = 20;
    seenFloat (window.traffic, Direction::in, "/channel/0/gain", 0.5, "motion");
    ASSERT_TRUE (window.open());

    const auto events = hc::events (window.port(), "/api/stream", 2);
    ASSERT_EQ (events.size(), 2u);
    std::vector<juce::var> payloads;
    for (const auto& event : events)
    {
        ASSERT_EQ (event.compare (0, 6, "data: "), 0);
        payloads.push_back (parsedBody (event.substr (6)));
    }
    ASSERT_GE (payloads.size(), 2u);
    EXPECT_TRUE (static_cast<bool> (payloads[0]["full"]));
    EXPECT_EQ (payloads[0]["history"].size(), 1);
    EXPECT_FALSE (static_cast<bool> (payloads[1]["full"]));
    EXPECT_EQ (payloads[1]["history"].size(), 0) << "nothing new since the first look";
    EXPECT_EQ (payloads[1]["rows"].size(), 1) << "rows come whole every tick";
}

TEST (CoreApi, StreamAsPython)
{
    // B0's first two events: the whole ring, then only what happened after the first look. The clock
    // is turned by the test, so each event has the instant B0's had; one tick a second gives the test
    // time to do it between the two.
    Window slow;
    slow.streamHz = 1;
    replayInto (slow);
    slow.clock.mono = static_cast<double> (apiFixture()["stream_start"]);
    ASSERT_TRUE (slow.open());

    const int fd = hc::connectTo (slow.port());
    hc::sendAll (fd, "GET /api/stream HTTP/1.0\r\n\r\n");
    std::string received;
    auto readEvent = [&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds (5);
        char buffer[65536];
        for (;;)
        {
            const auto head = received.find ("\r\n\r\n");
            const auto from = head == std::string::npos ? std::string::npos : head + 4;
            if (from != std::string::npos)
            {
                const auto stop = received.find ("\n\n", from);
                if (stop != std::string::npos)
                {
                    const auto event = received.substr (from, stop - from);
                    received.erase (from, stop + 2 - from);
                    return event;
                }
            }
            if (std::chrono::steady_clock::now() > end)
                return std::string();
            pollfd p { fd, POLLIN, 0 };
            if (::poll (&p, 1, 50) > 0)
            {
                const auto n = ::recv (fd, buffer, sizeof (buffer), 0);
                if (n <= 0)
                    return std::string();
                received.append (buffer, static_cast<size_t> (n));
            }
        }
    };

    const auto first = readEvent();
    ASSERT_EQ (first.compare (0, 6, "data: "), 0);
    // The script of the recording: between the looks, more traffic and a later instant.
    play (slow.traffic, apiFixture()["second_ops"]);
    slow.clock.mono = static_cast<double> (apiFixture()["stream_next"]);
    const auto second = readEvent();
    ::close (fd);
    ASSERT_EQ (second.compare (0, 6, "data: "), 0);

    const auto streamed = apiFixture()["responses"]["stream"];
    expectJsonEqual (streamed[0], parsedBody (first.substr (6)), "first event");
    expectJsonEqual (streamed[1], parsedBody (second.substr (6)), "second event");
}

TEST (CoreApi, TheStreamRunsAtFourHertzByDefault)
{
    Window window;
    ASSERT_TRUE (window.open());
    const auto start = std::chrono::steady_clock::now();
    const auto events = hc::events (window.port(), "/api/stream", 5);
    const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    ASSERT_EQ (events.size(), 5u);
    EXPECT_GT (seconds, 0.9) << "four gaps of a quarter second";
    EXPECT_LT (seconds, 2.5);
}

TEST (CoreApi, EveryHandlerReadsTheClockItWasGiven)
{
    // Traffic is handed `now`; if a handler read some other clock, the ages would not follow this one.
    Window window;
    window.devices.heard ("mixer", "abc", 400.0);
    window.clock.mono = 500.0;
    ASSERT_TRUE (window.open());
    EXPECT_DOUBLE_EQ (number (window.json ("/api/traffic")["at"]), 500.0);
    EXPECT_DOUBLE_EQ (number (window.json ("/api/unknown")["at"]), 500.0);
    EXPECT_DOUBLE_EQ (number (window.json ("/api/devices")["rows"][0]["age"]), 100.0);
    window.clock.mono = 530.0;
    EXPECT_DOUBLE_EQ (number (window.json ("/api/traffic")["at"]), 530.0);
    EXPECT_DOUBLE_EQ (number (window.json ("/api/devices")["rows"][0]["age"]), 130.0);
}

TEST (CoreApi, AStalledStreamReaderDoesNotBlockTheWriter)
{
    // A reader that opens the stream and never reads. The writer (Traffic::seen, on the serve thread)
    // must not wait for it, nor may any other reader of the window, nor the shutdown.
    Window window;
    const std::string padding (400, 'p');
    for (int i = 0; i < 9000; ++i)
        seenFloat (window.traffic, Direction::in, "/a/long/address/" + padding + std::to_string (i), 0.5, "motion");
    ASSERT_TRUE (window.open());

    // The first event must be bigger than anything the kernel can hold for this connection (the send
    // buffer's ceiling and the client's tiny receive buffer), or the writer would never meet EAGAIN.
    const auto eventSize = canonicalBytes (snapshotAsJson (window.traffic.snapshot (std::nullopt, window.clock.monotonic()), nullptr)).size();
    long long sendCeiling = 4 * 1024 * 1024;
    if (std::ifstream wmem ("/proc/sys/net/ipv4/tcp_wmem"); wmem)
    {
        long long low = 0, middle = 0;
        wmem >> low >> middle >> sendCeiling;
    }
    ASSERT_GT (static_cast<long long> (eventSize), sendCeiling + 1024 * 1024);

    const int stalled = hc::connectTo (window.port(), 1024);
    ASSERT_GE (stalled, 0);
    hc::sendAll (stalled, "GET /api/stream HTTP/1.0\r\n\r\n");
    std::this_thread::sleep_for (std::chrono::milliseconds (700));

    // The stall is real: the connection is open and most of the event has not reached the client.
    int queuedAtClient = 0;
    ::ioctl (stalled, FIONREAD, &queuedAtClient);
    EXPECT_EQ (window.server->openConnections(), 1);
    EXPECT_LT (static_cast<size_t> (queuedAtClient), eventSize / 10);

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i)
        seenFloat (window.traffic, Direction::in, "/channel/0/gain", static_cast<double> (i), "motion");
    EXPECT_LT (std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count(), 0.5);

    EXPECT_EQ (window.get ("/api/devices").status, 200) << "another reader is served while one is stuck";
    EXPECT_EQ (window.get ("/api/unknown").status, 200);
    // The two quick readers are gone (their close takes a moment); the stuck one is still held, not dropped.
    for (int i = 0; i < 100 && window.server->openConnections() != 1; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    EXPECT_EQ (window.server->openConnections(), 1) << "still stuck, not dropped";

    const auto stop = std::chrono::steady_clock::now();
    window.server->stop();
    EXPECT_LT (std::chrono::duration<double> (std::chrono::steady_clock::now() - stop).count(), 1.0) << "a stuck reader does not hold the shutdown";
    ::close (stalled);
}

//==============================================================================
// Task 21: what a list is, the bench, the status and the rig.

namespace
{
juce::var benchFixture()
{
    static const juce::var recorded = [] {
        juce::String error;
        auto value = parseJson (juce::File (A3_CORE_TEST_DATA "/parity/api/bench.json").loadFileAsString(), error);
        EXPECT_EQ (error, juce::String());
        return value;
    }();
    return recorded;
}

// A recorded Python value ["list", [["float", 0.5], ...]] fed to the traffic table the way Core feeds it.
ListValue listOf (const juce::var& items)
{
    ListValue out;
    for (const auto& item : *items.getArray())
        out.push_back (*valueOf (item).arg);
    return out;
}

void playBench (Traffic& traffic, const juce::var& ops)
{
    for (const auto& op : *ops.getArray())
    {
        const auto at = static_cast<double> (op["t"]);
        const auto peer = op["peer"].toString().toStdString();
        const auto address = op["address"].toString().toStdString();
        const auto& value = op["value"];
        const bool list = value[0].toString() == "list";
        if (op["op"].toString() == "seen")
        {
            if (list)
                traffic.seenList (directionOf (op["dir"].toString()), address, listOf (value[1]), peer, at);
            else
                traffic.seen (directionOf (op["dir"].toString()), address, valueOf (value).get(), peer, at);
        }
        else if (list)
            traffic.unknownList (address, listOf (value[1]), peer, at);
        else
            traffic.unknown (address, valueOf (value).get(), peer, at);
    }
}

// The bench's window: addCoreRoutes with a stand-in for the service's sender, and a view to read.
struct BenchWindow
{
    CoreView view;
    std::vector<std::pair<std::string, OscMessage>> sent;
    bool withSend = true, withView = true;
    std::unique_ptr<HttpServer> server;

    bool open()
    {
        Routes routes;
        addCoreRoutes (routes, withView ? &view : nullptr,
                       withSend ? BenchSend ([this] (const std::string& to, const OscMessage& message) { sent.emplace_back (to, message); })
                                : BenchSend());
        server = std::make_unique<HttpServer> (loopbackAddress() + ":0", std::move (routes));
        return server->start ([] (const std::string&, bool) {});
    }

    ~BenchWindow()
    {
        if (server)
            server->stop();
    }

    hc::Reply get (const std::string& path) const { return hc::get (server->port(), path); }
    hc::Reply post (const juce::var& body) const { return hc::post (server->port(), "/api/send", juce::JSON::toString (body, true).toStdString()); }
    hc::Reply postRaw (const std::string& body) const { return hc::post (server->port(), "/api/send", body); }

    static juce::var request (const std::string& to, const std::string& address, const std::string& value)
    {
        auto* made = new juce::DynamicObject();
        made->setProperty ("to", juce::String (to));
        made->setProperty ("address", juce::String (address));
        made->setProperty ("value", juce::String (value));
        return juce::var (made);
    }
};

std::string problemOf (const hc::Reply& reply) { return parsedBody (reply.body)["problem"].toString().toStdString(); }
}

// parse_value.

TEST (CoreApi, ABareNumberIsANumber)
{
    EXPECT_EQ (parseValue ("1"), pyInt (1));
    EXPECT_EQ (parseValue ("1").tag, 'i');
    EXPECT_EQ (parseValue ("0.5"), pyFloat (0.5));
    EXPECT_EQ (parseValue ("-180"), pyInt (-180));
}

TEST (CoreApi, AQuotedNumberIsText)
{
    EXPECT_EQ (parseValue ("\"1\""), pyStr ("1"));
}

TEST (CoreApi, ABareWordIsText)
{
    EXPECT_EQ (parseValue ("high_pass"), pyStr ("high_pass"));
}

TEST (CoreApi, SurroundingSpaceDoesNotChangeTheType)
{
    EXPECT_EQ (parseValue ("  1  "), pyInt (1));
}

TEST (CoreApi, NothingTypedIsAnErrorNotAnEmptyString)
{
    EXPECT_THROW (parseValue ("   "), std::invalid_argument);
    EXPECT_THROW (parseValue (""), std::invalid_argument);
}

TEST (CoreApi, AnIntegerBeyondInt64IsRefusedNotWrapped)
{
    // python-osc cannot encode it either (BuildError): the bench says no instead of sending nonsense.
    EXPECT_THROW (parseValue ("99999999999999999999"), std::out_of_range);
    EXPECT_EQ (parseValue ("4294967296"), pyInt (4294967296LL));
    EXPECT_EQ (parseValue ("4294967296").tag, 'h');
}

// The bench.

TEST (CoreApi, ANumberArrivesAsANumber)
{
    BenchWindow window;
    ASSERT_TRUE (window.open());
    const auto reply = window.post (BenchWindow::request ("mixer", "/channel/0/pfl", "1"));
    EXPECT_EQ (reply.status, 200);
    ASSERT_EQ (window.sent.size(), 1u);
    EXPECT_EQ (window.sent[0].first, "mixer");
    EXPECT_EQ (window.sent[0].second, (OscMessage { "/channel/0/pfl", { pyInt (1) } }));
    const auto answer = parsedBody (reply.body);
    EXPECT_EQ (answer["address"].toString(), "/channel/0/pfl");
    EXPECT_EQ (static_cast<int> (answer["value"]), 1);
}

TEST (CoreApi, AQuotedNumberArrivesAsText)
{
    BenchWindow window;
    ASSERT_TRUE (window.open());
    window.post (BenchWindow::request ("mixer", "/channel/0/pfl", "\"1\""));
    ASSERT_EQ (window.sent.size(), 1u);
    EXPECT_EQ (window.sent[0].second.args, (std::vector<OscArg> { pyStr ("1") }));
}

TEST (CoreApi, AnEmptyAddressSendsNothing)
{
    BenchWindow window;
    ASSERT_TRUE (window.open());
    const auto reply = window.post (BenchWindow::request ("mixer", "", "1"));
    EXPECT_EQ (reply.status, 400);
    EXPECT_TRUE (window.sent.empty());
    EXPECT_FALSE (problemOf (reply).empty());
}

TEST (CoreApi, ABodyThatIsNotJsonSendsNothing)
{
    BenchWindow window;
    ASSERT_TRUE (window.open());
    const auto reply = window.postRaw ("not json at all");
    EXPECT_EQ (reply.status, 400);
    EXPECT_TRUE (window.sent.empty());
    EXPECT_FALSE (problemOf (reply).empty());
}

TEST (CoreApi, AnUnknownDestinationSendsNothing)
{
    BenchWindow window;
    ASSERT_TRUE (window.open());
    const auto reply = window.post (BenchWindow::request ("nowhere", "/a", "1"));
    EXPECT_EQ (reply.status, 400);
    EXPECT_TRUE (window.sent.empty());
    EXPECT_EQ (problemOf (reply), "unknown destination: nowhere");
}

TEST (CoreApi, PostingToAReadOnlyWindowIsRefusedNotACrash)
{
    BenchWindow window;
    window.withSend = false;
    ASSERT_TRUE (window.open());
    const auto reply = window.post (BenchWindow::request ("mixer", "/a", "1"));
    EXPECT_EQ (reply.status, 400);
    EXPECT_EQ (problemOf (reply), "this window can only watch");
}

TEST (CoreApi, SendRefusesReaperLikeAnyUnknownTargetAndGarbage)
{
    BenchWindow window;
    ASSERT_TRUE (window.open());
    // REAPER left the Core on 2026-10-27: it is a destination like any other the bench does not know.
    auto reply = window.post (BenchWindow::request ("reaper", "/a", "1"));
    EXPECT_EQ (reply.status, 400);
    EXPECT_EQ (problemOf (reply), "unknown destination: reaper");

    EXPECT_EQ (window.post (BenchWindow::request ("mixer", "no-slash", "1")).status, 400);
    EXPECT_EQ (problemOf (window.post (BenchWindow::request ("mixer", "no-slash", "1"))), "an address starts with /");
    EXPECT_EQ (problemOf (window.post (BenchWindow::request ("mixer", "/a", "   "))), "nothing typed");
    EXPECT_EQ (problemOf (window.post (BenchWindow::request ("mixer", "/a", "99999999999999999999"))).empty(), false);
    EXPECT_EQ (window.postRaw ("[1, 2]").status, 400);
    EXPECT_EQ (window.postRaw ("").status, 400);
    EXPECT_EQ (window.postRaw ("{\"to\": \"mixer\"}").status, 400);
    EXPECT_EQ (window.postRaw ("{\"to\": 5, \"address\": \"/a\", \"value\": \"1\"}").status, 400);
    EXPECT_TRUE (window.sent.empty());
}

TEST (CoreApi, ASenderThatRefusesIsAnswered400)
{
    Routes routes;
    addCoreRoutes (routes, nullptr, [] (const std::string&, const OscMessage&) { throw SendRefused ("the bench is busy"); });
    HttpServer server (loopbackAddress() + ":0", std::move (routes));
    ASSERT_TRUE (server.start ([] (const std::string&, bool) {}));
    const auto reply = hc::post (server.port(), "/api/send", "{\"to\": \"self\", \"address\": \"/a\", \"value\": \"1\"}");
    server.stop();
    EXPECT_EQ (reply.status, 400);
    EXPECT_EQ (problemOf (reply), "the bench is busy");
}

TEST (CoreApi, TheBenchAsPython)
{
    // Every body B1's window was posted: the status, the message that went out and the answer; where B1
    // refuses, we refuse too (its wording is German or Python's own, ours is English, so only "nothing typed",
    // which both say, is compared as text).
    for (const auto& recorded : *benchFixture()["sends"].getArray())
    {
        BenchWindow window;
        ASSERT_TRUE (window.open());
        const auto name = recorded["name"].toString().toStdString();
        const auto reply = recorded.hasProperty ("raw") ? window.postRaw (recorded["raw"].toString().toStdString())
                                                         : window.post (recorded["body"]);
        EXPECT_EQ (reply.status, static_cast<int> (recorded["status"])) << name;
        const auto expected = recorded["reply"];
        const auto got = parsedBody (reply.body);
        if (reply.status != 200)
        {
            EXPECT_TRUE (window.sent.empty()) << name;
            EXPECT_TRUE (got["problem"].isString()) << name;
            if (expected["problem"].toString() == "nothing typed")
                EXPECT_EQ (got["problem"].toString(), "nothing typed") << name;
            continue;
        }
        expectJsonEqual (expected, got, name);
        ASSERT_EQ (window.sent.size(), 1u) << name;
        const auto& wanted = recorded["sent"][0];
        const auto& [to, message] = window.sent[0];
        EXPECT_EQ (to, wanted[0].toString().toStdString()) << name;
        EXPECT_EQ (message.address, expected["address"].toString().toStdString()) << name;
        ASSERT_EQ (message.args.size(), 1u) << name;
        const auto& arg = message.args[0];
        const auto type = wanted[3].toString();
        EXPECT_EQ (type == "int" ? "i" : type == "float" ? "f" : "s", std::string (1, arg.tag == 'h' ? 'i' : arg.tag)) << name;
    }
}

// The status.

TEST (CoreApi, TheStatusIsServed)
{
    BenchWindow window;
    window.view.publish ({ canonicalBytes (benchFixture()["responses"]["status"]), "{}" });
    ASSERT_TRUE (window.open());
    const auto reply = window.get ("/api/status");
    EXPECT_EQ (reply.status, 200);
    EXPECT_EQ (reply.headers.at ("content-type"), "application/json");
    expectJsonEqual (benchFixture()["responses"]["status"], parsedBody (reply.body), "/api/status");
}

TEST (CoreApi, WithoutAStatusItIsEmpty)
{
    BenchWindow window;
    window.withView = false;
    ASSERT_TRUE (window.open());
    const auto empty = parsedBody (window.get ("/api/status").body);
    ASSERT_TRUE (empty.isObject());
    EXPECT_EQ (empty.getDynamicObject()->getProperties().size(), 0);
    expectJsonEqual (benchFixture()["responses"]["status_without"], parsedBody (window.get ("/api/status").body), "no status");
    EXPECT_EQ (window.get ("/api/rig").status, 200);
}

// A list in the traffic table, as B1's window shows it.

TEST (CoreApi, AListIsInTheTrafficTableAsB1HasIt)
{
    // One table per look: every look stamps its instant, and B1's recorded ones are the scripted 100.0.
    for (const char* name : { "traffic", "unknown" })
    {
        Traffic traffic;
        playBench (traffic, benchFixture()["list_ops"]);
        TestClock clock;
        clock.mono = static_cast<double> (benchFixture()["fetched_at"]);
        Routes routes;
        addMonitorRoutes (routes, { nullptr, traffic, nullptr, shippedRegister(), clock });
        HttpServer server (loopbackAddress() + ":0", std::move (routes));
        ASSERT_TRUE (server.start ([] (const std::string&, bool) {}));
        const auto asked = parsedBody (hc::get (server.port(), std::string ("/api/") + name).body);
        server.stop();
        expectJsonEqual (benchFixture()["responses"][name], asked, std::string ("/api/") + name);
    }
}

TEST (CoreApi, ASnapshotWithAListHasItInTheHistoryToo)
{
    Traffic traffic;
    traffic.seenList (Direction::in, "/a", { pyFloat (0.5), pyStr ("x") }, "motion", 1.0);
    const auto json = snapshotAsJson (traffic.snapshot (std::nullopt, 2.0), nullptr);
    EXPECT_EQ (json["history"][0]["value"].toString(), "[0.5, 'x']");
    EXPECT_EQ (json["history"][0]["type"].toString(), "list");
    EXPECT_EQ (json["rows"][0]["last_type"].toString(), "list");
}

namespace
{
const Endpoint prodjApp { "192.168.8.60", 7773 };   // the desk port: the origin is known by it

juce::var parsedRig (const CoreRig& rig)
{
    return parsedBody (rig.core->view().texts().rig);
}

juce::var property (const juce::var& object, const char* name) { return object[juce::Identifier (name)]; }

std::vector<juce::var> rowsOf (const juce::var& array)
{
    std::vector<juce::var> out;
    for (const auto& item : *array.getArray())
        out.push_back (item);
    return out;
}
}

// Core's own traffic rows carry a list too.

TEST (CoreApi, ASendOfSeveralArgumentsIsAListInTheTable)
{
    CoreRig rig;
    rig.motionHello();
    rig.stemdeckHello ("127.0.0.2");
    rig.hello (prodjApp, "prodj");
    rig.osc (prodjApp, "/prodj/live", { pyInt (1) });
    rig.osc (prodjApp, "/stemdeck/pointer", { pyInt (0), pyFloat (0.25), pyFloat (0.75) });
    bool found = false;
    for (const auto& row : rig.core->traffic().snapshot (std::nullopt, rig.clock.monotonic()).rows)
    {
        if (row.address == "/stemdeck/pointer" && row.direction == Direction::out)
        {
            found = true;
            ASSERT_TRUE (row.lastList.has_value());
            EXPECT_EQ (row.lastType, "list");
            EXPECT_EQ (row.lastList->size(), 3u);
        }
    }
    EXPECT_TRUE (found);
}

TEST (CoreApi, APointerArrivesAsAListEvenWhenRefused)
{
    // B1 records list(osc_arguments) for a pointer, and records it before the LIVE gate.
    CoreRig rig;
    rig.hello (prodjApp, "prodj");
    rig.osc (prodjApp, "/motion/pointer", { pyInt (1), pyFloat (0.5), pyFloat (0.5) });   // LIVE is off: refused
    for (const auto& row : rig.core->traffic().snapshot (std::nullopt, rig.clock.monotonic()).rows)
        if (row.address == "/motion/pointer" && row.direction == Direction::in)
        {
            EXPECT_EQ (row.lastType, "list");
            ASSERT_TRUE (row.lastList.has_value());
            EXPECT_EQ (*row.lastList, (ListValue { pyInt (1), OscArg { 'f', 0.5f }, OscArg { 'f', 0.5f } }));   // as the wire decodes them
            return;
        }
    FAIL() << "the pointer is not in the table";
}

TEST (CoreApi, AMalformedPreviewIsAnUnknownRowHoldingTheList)
{
    CoreRig rig;
    rig.stemdeckHello ("127.0.0.2");
    rig.osc ({ "127.0.0.2", 6000 }, "/stemdeck/ahead", { pyStr ("drop"), pyInt (3) });
    for (const auto& row : rig.core->traffic().unknownSnapshot (rig.clock.monotonic()).rows)
        if (row.address == "/stemdeck/ahead")
        {
            EXPECT_EQ (row.lastType, "list");
            ASSERT_TRUE (row.lastList.has_value());
            EXPECT_EQ (*row.lastList, (ListValue { pyStr ("drop"), pyInt (3) }));
            return;
        }
    FAIL() << "the preview is not in the unknown table";
}

// The status and the rig.

TEST (CoreApi, StatusSaysTheEngine)
{
    CoreRig rig;
    const auto status = rig.core->statusJson();
    EXPECT_EQ (status["audio_engine"].toString(), "engine");
    EXPECT_EQ (status["reason"].toString(), "");
    rig.core->publishView();
    // B1's status, word for word, and beside it since 2026-10-30 the program's own version.
    const auto published = parsedBody (rig.core->view().texts().status);
    for (const auto& [key, value] : benchFixture()["responses"]["status"].getDynamicObject()->getProperties())
        expectJsonEqual (value, published[key], "published status: " + key.toString().toStdString());

    BenchWindow window;
    window.view.publish (rig.core->view().texts());
    ASSERT_TRUE (window.open());
    expectJsonEqual (published, parsedBody (window.get ("/api/status").body), "/api/status");
}

// The Canvas shows Core's version beside the engine row on any machine: Core says it in its status.
TEST (CoreApi, StatusSaysTheProgramsVersion)
{
    CoreRig rig;
    const auto version = rig.core->statusJson()["version"];
    ASSERT_TRUE (version.isString());
    EXPECT_EQ (version.toString().toStdString(), std::string (programVersion()));
    EXPECT_FALSE (version.toString().isEmpty());
}

// device.version (2026-10-30): what each device runs, kept per device and sending host.
TEST (CoreApi, RigCarriesWhatEachDeviceReports)
{
    CoreRig rig (CoreRig::WithOptions {}, corerig::onThePackage());
    const Endpoint motionAt { "192.168.8.20", 40002 }, deckB { "192.168.8.13", 7000 }, mixerAt { "192.168.8.11", 7001 };
    rig.at (10.0);
    rig.hello (motionAt, "motion");
    rig.osc (motionAt, "/device/version", { pyStr ("motion"), pyStr ("03.0+855"), pyStr ("v03.2+4"), pyStr ("192.168.9.20") });
    rig.hello (deckB, "stemdeck");
    rig.osc (deckB, "/device/version", { pyStr ("stemdeck"), pyStr ("03.0+281"), pyStr (""), pyStr ("192.168.9.23") });
    rig.osc (rig.deckA, "/device/version", { pyStr ("stemdeck"), pyStr ("03.0+280"), pyStr (""), pyStr ("192.168.9.10") });
    rig.osc (rig.analyzer, "/device/version", { pyStr ("beat-analyzer"), pyStr ("03.0+12") });   // the old form: name and version
    rig.hello (mixerAt, "mixer");   // says hello, not yet its version
    rig.at (12.0);
    rig.core->publishView();

    const auto rigJson = parsedRig (rig);
    std::map<std::string, juce::var> devices;
    for (const auto& row : rowsOf (property (rigJson, "devices")))
        devices[row["device"].toString().toStdString()] = row;
    ASSERT_EQ (devices.count ("motion"), 1u);
    EXPECT_EQ (devices["motion"]["host"].toString(), "192.168.8.20");
    EXPECT_EQ (devices["motion"]["version"].toString(), "03.0+855");
    EXPECT_EQ (devices["motion"]["firmware"].toString(), "v03.2+4");
    EXPECT_EQ (devices["motion"]["audio"].toString(), "192.168.9.20");
    EXPECT_EQ (devices["stemdeck"]["host"].toString(), "192.168.8.13") << "the host of its last hello";
    EXPECT_EQ (devices["stemdeck"]["version"].toString(), "03.0+281");
    EXPECT_EQ (devices["stemdeck"]["firmware"].toString(), "");
    ASSERT_EQ (devices.count ("mixer"), 1u);
    EXPECT_EQ (devices["mixer"]["host"].toString(), "192.168.8.11");
    EXPECT_TRUE (devices["mixer"]["version"].isVoid()) << "not reported";
    EXPECT_TRUE (devices["mixer"]["firmware"].isVoid());
    EXPECT_TRUE (devices["mixer"]["audio"].isVoid());

    std::vector<std::string> versions;
    for (const auto& row : rowsOf (property (rigJson, "versions")))
        versions.push_back (row["device"].toString().toStdString() + "@" + row["host"].toString().toStdString() + " "
                            + row["version"].toString().toStdString() + " fw=" + row["firmware"].toString().toStdString()
                            + " audio=" + row["audio"].toString().toStdString() + " age=" + juce::String (static_cast<double> (row["age"]), 1).toStdString());
    EXPECT_EQ (versions, (std::vector<std::string> { "beat-analyzer@127.0.0.1 03.0+12 fw= audio= age=2.0",
                                                     "motion@192.168.8.20 03.0+855 fw=v03.2+4 audio=192.168.9.20 age=2.0",
                                                     "stemdeck@127.0.0.2 03.0+280 fw= audio=192.168.9.10 age=2.0",
                                                     "stemdeck@192.168.8.13 03.0+281 fw= audio=192.168.9.23 age=2.0" }));
}

// A desk says "desk" and is shown as desk@<host>, as its hello is; a version without a name is nobody's.
TEST (CoreApi, ADesksVersionIsItsRows)
{
    CoreRig rig (CoreRig::WithOptions {}, corerig::onThePackage());
    const Endpoint deskAt { "192.168.8.40", 7773 };
    rig.hello (deskAt, "desk");
    rig.osc (deskAt, "/device/version", { pyStr ("desk"), pyStr ("03.0+90"), pyStr ("v03.0+2"), pyStr ("") });
    rig.osc (deskAt, "/device/version", { pyStr ("") , pyStr ("1") });
    rig.osc (deskAt, "/device/version", { pyStr ("lonely") });
    rig.core->publishView();
    const auto rigJson = parsedRig (rig);
    const auto versions = rowsOf (property (rigJson, "versions"));
    ASSERT_EQ (versions.size(), 1u);
    EXPECT_EQ (versions[0]["device"].toString(), "desk@192.168.8.40");
    for (const auto& row : rowsOf (property (rigJson, "devices")))
        if (row["device"].toString() == "desk@192.168.8.40")
            EXPECT_EQ (row["firmware"].toString(), "v03.0+2");
}

// B0's truth has no device.version: Core keeps no versions and says nothing about it.
TEST (CoreApi, ATruthWithoutTheWordKeepsNoVersions)
{
    CoreRig rig;
    rig.osc (rig.remoteMotion, "/device/version", { pyStr ("motion"), pyStr ("03.0+855"), pyStr (""), pyStr ("") });
    rig.core->publishView();
    EXPECT_TRUE (rowsOf (property (parsedRig (rig), "versions")).empty());
}

TEST (CoreApi, RigShowsWhoIsThere)
{
    CoreRig rig;
    rig.at (10.0);
    rig.hello (rig.remoteMotion, "motion");
    rig.hello ({ "192.168.8.40", 7773 }, "desk");
    rig.hello (prodjApp, "prodj");
    rig.osc (prodjApp, "/prodj/live", { pyInt (1) });
    rig.stemdeckHello ("127.0.0.2");
    rig.deckHello (1, rig.deckA);
    rig.engineHello ("boot-7");
    rig.at (12.5);
    rig.core->publishView();

    const auto rigJson = parsedRig (rig);
    const auto motion = property (rigJson, "motion");
    EXPECT_EQ (motion["followed"].toString(), "192.168.8.20");
    EXPECT_TRUE (static_cast<bool> (motion["remote"]));
    EXPECT_DOUBLE_EQ (static_cast<double> (motion["last_heard"]), 2.5);

    const auto desks = rowsOf (property (rigJson, "desks"));
    ASSERT_EQ (desks.size(), 2u);
    EXPECT_EQ (desks[0]["host"].toString(), "192.168.8.40");
    EXPECT_EQ (desks[0]["kind"].toString(), "desk");
    EXPECT_EQ (desks[0]["name"].toString(), "desk@192.168.8.40");
    EXPECT_DOUBLE_EQ (static_cast<double> (desks[0]["last_heard"]), 2.5);
    EXPECT_EQ (desks[1]["kind"].toString(), "prodj");
    EXPECT_EQ (desks[1]["name"].toString(), "prodj@192.168.8.60");

    const auto decks = rowsOf (property (rigJson, "decks"));
    ASSERT_GE (decks.size(), 2u);   // one row for every deck the selector knows
    EXPECT_EQ (static_cast<int> (decks[0]["deck"]), 1);
    EXPECT_TRUE (static_cast<bool> (decks[0]["present"]));
    EXPECT_EQ (decks[0]["host"].toString(), "127.0.0.2");
    EXPECT_EQ (static_cast<int> (decks[0]["port"]), deckPort (b0::truth(), 1));
    EXPECT_TRUE (decks[0]["channel"].isVoid());   // on no channel: the channels play analog
    EXPECT_DOUBLE_EQ (static_cast<double> (decks[0]["last_heard"]), 2.5);
    EXPECT_EQ (static_cast<int> (decks[1]["deck"]), 2);
    EXPECT_FALSE (static_cast<bool> (decks[1]["present"]));
    EXPECT_TRUE (decks[1]["host"].isVoid());
    EXPECT_EQ (static_cast<int> (decks[1]["port"]), deckPort (b0::truth(), 2));
    EXPECT_TRUE (decks[1]["last_heard"].isVoid());

    EXPECT_EQ (property (rigJson, "stemdeck")["host"].toString(), "127.0.0.2");
    EXPECT_TRUE (static_cast<bool> (property (rigJson, "prodj_live")));

    const auto engine = property (rigJson, "engine");
    EXPECT_TRUE (static_cast<bool> (engine["heard"]));
    EXPECT_EQ (engine["boot_id"].toString(), "boot-7");
    EXPECT_TRUE (static_cast<bool> (engine["open"]));

    const auto devices = rowsOf (property (rigJson, "devices"));
    ASSERT_FALSE (devices.empty());
    expectJsonEqual (rig.core->devices().rigRows (rig.clock.monotonic()), property (rigJson, "devices"), "the devices with their reports");
}

TEST (CoreApi, RigOfAnEmptyRigSaysNobodyIsThere)
{
    CoreRig rig;
    rig.core->publishView();
    const auto rigJson = parsedRig (rig);
    EXPECT_TRUE (property (rigJson, "motion")["followed"].isVoid());
    EXPECT_FALSE (static_cast<bool> (property (rigJson, "motion")["remote"]));
    EXPECT_TRUE (property (rigJson, "motion")["last_heard"].isVoid());
    EXPECT_TRUE (rowsOf (property (rigJson, "desks")).empty());
    EXPECT_TRUE (property (rigJson, "stemdeck")["host"].isVoid());
    EXPECT_FALSE (static_cast<bool> (property (rigJson, "prodj_live")));
    EXPECT_FALSE (static_cast<bool> (property (rigJson, "engine")["heard"]));
    EXPECT_TRUE (property (rigJson, "engine")["boot_id"].isVoid());
    EXPECT_FALSE (static_cast<bool> (property (rigJson, "engine")["open"]));
}

TEST (CoreApi, AnEngineThatCameBackIsNotOpenUntilTheRecallOpensIt)
{
    CoreRig rig;
    rig.engineHello ("boot-1");
    rig.core->publishView();
    EXPECT_TRUE (static_cast<bool> (property (parsedRig (rig), "engine")["open"]));
    rig.engineHello ("boot-1");   // the repeated hello changes nothing
    rig.core->publishView();
    EXPECT_TRUE (static_cast<bool> (property (parsedRig (rig), "engine")["open"]));

    // A different boot id is a new engine: closed until the recall after the replay opens it. The rig
    // feeds Core's own sends back at once, so the new hello is split to look between the two steps.
    const auto hello = encodeMessage ({ "/engine/hello", { pyStr ("boot-2") } });
    rig.core->handlePacket (rig.engine, hello.data(), hello.size());   // replay queued, recall not yet handled
    rig.core->publishView();
    const auto engine = property (parsedRig (rig), "engine");
    EXPECT_EQ (engine["boot_id"].toString(), "boot-2");
    EXPECT_FALSE (static_cast<bool> (engine["open"]));
    rig.osc (rig.engine, "/nothing");   // feeds the queued replay and the recall back
    rig.core->publishView();
    EXPECT_TRUE (static_cast<bool> (property (parsedRig (rig), "engine")["open"]));
}

TEST (CoreApi, TheViewIsPublishedByTheTickNoOftenerThanItIsDue)
{
    CoreRig rig;
    rig.at (1.0);
    rig.tick();
    const auto first = rig.core->view().texts().rig;
    rig.hello (rig.remoteMotion, "motion");
    rig.at (1.1);
    rig.tick();
    EXPECT_EQ (rig.core->view().texts().rig, first) << "not yet due";
    rig.at (1.0 + Core::publishEvery);
    rig.tick();
    EXPECT_NE (rig.core->view().texts().rig, first);
}

TEST (CoreApi, TheRigIsOnlyBuiltOnTheServeThread)
{
    // P8: an HTTP thread never calls into Core. The owning-thread check is what would say so, loudly.
    CoreRig rig;
    std::string status, rigText, publish, bench;
    std::thread other ([&] {
        const auto what = [] (const std::function<void()>& call) {
            try
            {
                call();
            }
            catch (const std::runtime_error& problem)
            {
                return std::string (problem.what());
            }
            return std::string();
        };
        status = what ([&] { rig.core->statusJson(); });
        rigText = what ([&] { rig.core->rigJson(); });
        publish = what ([&] { rig.core->publishView(); });
        bench = what ([&] { rig.core->sendFromBench ("mixer", { "/a", { pyInt (1) } }); });
    });
    other.join();
    for (const auto* said : { &status, &rigText, &publish, &bench })
        EXPECT_NE (said->find ("does not own"), std::string::npos) << *said;
    EXPECT_NO_THROW (rig.core->rigJson());   // the owner may
}

TEST (CoreApi, TheWindowAnswersFromTheCopyAndNeverCallsIntoCore)
{
    CoreRig rig;
    rig.hello (rig.remoteMotion, "motion");
    rig.core->publishView();
    BenchWindow window;
    window.view.publish (rig.core->view().texts());
    ASSERT_TRUE (window.open());
    // The handlers hold a CoreView and a sender, not Core: they have nothing to call into.
    const auto answered = parsedBody (window.get ("/api/rig").body);
    EXPECT_EQ (property (answered, "motion")["followed"].toString(), "192.168.8.20");
    // ... and the copy is a copy: a change in Core is not in it before the next publishing.
    rig.hello ({ "192.168.8.40", 7773 }, "desk");
    EXPECT_TRUE (rowsOf (property (parsedBody (window.get ("/api/rig").body), "desks")).empty());
}

TEST (CoreApi, TheBenchSendsMixerAndMotionAsCoreSends)
{
    CoreRig rig;
    rig.clear();
    rig.core->sendFromBench ("mixer", { "/bench/one", { pyFloat (0.5) } });
    rig.core->sendFromBench ("motion", { "/bench/two", { pyStr ("1") } });
    EXPECT_EQ (rig.to ("mixer"), (std::vector<std::string> { "/bench/one f:0.5" }));
    EXPECT_EQ (rig.to ("motion"), (std::vector<std::string> { "/bench/two s:1" }));
    bool counted = false;
    for (const auto& row : rig.core->traffic().snapshot (std::nullopt, rig.clock.monotonic()).rows)
        counted = counted || (row.address == "/bench/one" && row.direction == Direction::out && row.peer == "mixer");
    EXPECT_TRUE (counted);
    EXPECT_THROW (rig.core->sendFromBench ("reaper", { "/x", {} }), std::invalid_argument);
    EXPECT_THROW (rig.core->sendFromBench ("self", { "/x", {} }), std::invalid_argument);
}

// Core's deck selector counts the package truth's deck range (stemdeck.deck-hello): four decks, A-D, and a
// channel's input ring turns A B C D ANALOG.
TEST (CoreApi, ThePackagesTruthGivesFourDecks)
{
    const auto truth = std::get<Truth> (Truth::load (juce::File (A3_PACKAGE_TRUTH), std::nullopt));
    CoreRig::Options options;
    options.truth = &truth;
    CoreRig rig (CoreRig::WithOptions {}, options);
    rig.core->publishView();
    const auto decks = rowsOf (property (parsedRig (rig), "decks"));
    ASSERT_EQ (decks.size(), 4u);
    for (int deck = 1; deck <= 4; ++deck)
        EXPECT_EQ (static_cast<int> (decks[(size_t) deck - 1]["deck"]), deck);

    rig.clear();
    for (int step = 0; step < 5; ++step)
        rig.osc (rig.desk, "/channel/1/stem/turn", { pyStr ("1") });
    EXPECT_EQ (corerig::startingWith (rig.to ("motion"), "/channel/1/source/cursor"),
               (std::vector<std::string> { "/channel/1/source/cursor i:1", "/channel/1/source/cursor i:2",
                                           "/channel/1/source/cursor i:3", "/channel/1/source/cursor i:4",
                                           "/channel/1/source/cursor i:0" }));
}

// Where the delay's tempo comes from, as the last look chose it (Ruling A4).
TEST (CoreApi, RigNamesWhereTheDelaysTempoComesFrom)
{
    const auto truth = std::get<Truth> (Truth::load (juce::File (A3_PACKAGE_TRUTH), std::nullopt));
    CoreRig::Options options;
    options.truth = &truth;
    CoreRig rig (CoreRig::WithOptions {}, options);
    rig.core->publishView();
    EXPECT_EQ (property (parsedRig (rig), "tempo_source").toString(), "held");
    rig.osc ({ "127.0.0.3", 6001 }, "/stemdeck/2/tempo", { pyFloat (126.0), pyInt (1), pyInt (1) });
    rig.core->publishView();
    EXPECT_EQ (property (parsedRig (rig), "tempo_source").toString(), "master deck 2");
}
