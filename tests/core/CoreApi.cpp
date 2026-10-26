// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of the monitor half of a3-core's tools/tests/test_core_web.py and the API case of
// test_core_devices.py (B0 f3345dc7feeb): the JSON a3-core serves for people, and the truth that
// StemDeck and the desks fetch. Plus what tools/parity/record_api.py recorded from B0's window over
// real HTTP for one scripted state. The HTML page, the bench (/api/send) and the status are other
// tasks'. Every test talks real HTTP to an ephemeral port on loopback.
#include <atomic>
#include <sys/ioctl.h>

#include <chrono>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>

#include "B0Truth.h"
#include "CoreApi.h"
#include "HttpClient.h"
#include "HttpServer.h"
#include "StateRecorded.h"
#include "TrafficRecorded.h"

using namespace a3core;
using namespace recordedtraffic;
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
