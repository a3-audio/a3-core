// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_traffic.py (B0 f3345dc7feeb): what went past Core, counted --
// and nothing more on the hot path. Plus the recordings of tools/parity/record_traffic.py: osc_bytes,
// peer_name and a3_core_web.as_json for a scripted traffic.
#include <atomic>
#include <set>
#include <thread>

#include <gtest/gtest.h>

#include "TrafficRecorded.h"

using namespace a3core;
using namespace recordedtraffic;

namespace
{
const std::map<std::string, std::string> rig { { "mixer", "192.168.43.55" }, { "motion", "192.168.43.54" }, { "reaper", "127.0.0.1" } };

TrafficSnapshot look (Traffic& traffic) { return traffic.snapshot (std::nullopt, tick()); }
UnknownSnapshot lookUnknown (Traffic& traffic) { return traffic.unknownSnapshot (tick()); }

void unknownFloat (Traffic& traffic, const std::string& address, double value, const std::string& peer = "reaper")
{
    unknownOf (traffic, address, pyFloat (value), peer);
}

std::string numbered (const char* before, int i, const char* after) { return before + std::to_string (i) + after; }
}

// NamingWhoItWas

TEST (Traffic, AHostOnTheRigGetsItsDeviceName)
{
    EXPECT_EQ (peerName ("192.168.43.55", rig), "mixer");
    EXPECT_EQ (peerName ("192.168.43.54", rig), "motion");
}

TEST (Traffic, AStrangerIsShownAsItself) { EXPECT_EQ (peerName ("10.0.0.9", rig), "10.0.0.9"); }

TEST (Traffic, TwoDevicesOnOneHostAreNotGuessedAt)
{
    const std::map<std::string, std::string> oneBox { { "mixer", "127.0.0.1" }, { "motion", "127.0.0.1" }, { "reaper", "127.0.0.1" } };
    EXPECT_EQ (peerName ("127.0.0.1", oneBox), "127.0.0.1 (mehrdeutig)");
}

TEST (Traffic, CoresOwnPortBreaksTheTie)
{
    const std::map<std::string, std::string> motionAndReaper { { "mixer", "192.168.8.11" }, { "motion", "127.0.0.1" }, { "reaper", "127.0.0.1" } };
    EXPECT_EQ (peerName ("127.0.0.1", motionAndReaper, commanders), "motion");
    EXPECT_EQ (peerName ("127.0.0.1", motionAndReaper, answerers), "reaper");
}

TEST (Traffic, ANarrowedListStillRefusesARealTie)
{
    const std::map<std::string, std::string> bothControllers { { "mixer", "127.0.0.1" }, { "motion", "127.0.0.1" }, { "reaper", "192.168.8.20" } };
    EXPECT_EQ (peerName ("127.0.0.1", bothControllers, commanders), "127.0.0.1 (mehrdeutig)");
}

TEST (Traffic, ADeviceThatCannotBeOnThisPortIsNotNamed) { EXPECT_EQ (peerName ("127.0.0.1", rig, commanders), "127.0.0.1"); }

TEST (Traffic, PeerNameAsPython)
{
    for (const auto& item : *fixture ("traffic.json")["peers"].getArray())
    {
        std::map<std::string, std::string> peers;
        for (const auto& property : item["peers"].getDynamicObject()->getProperties())
            peers[property.name.toString().toStdString()] = property.value.toString().toStdString();
        std::optional<std::vector<std::string>> only;
        if (const auto* names = item["only"].getArray())
        {
            only.emplace();
            for (const auto& name : *names)
                only->push_back (name.toString().toStdString());
        }
        EXPECT_EQ (peerName (item["host"].toString().toStdString(), peers, only), item["name"].toString().toStdString()) << dump (item);
    }
}

// CountingByAddressAndDirection

TEST (Traffic, TheSameAddressInBothDirectionsIsTwoRows)
{
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "motion");
    seenFloat (traffic, Direction::out, "/channel/0/gain", 0.5, "mixer");
    const auto snapshot = look (traffic);
    ASSERT_EQ (snapshot.rows.size(), 2u);
    EXPECT_EQ (rowOf (snapshot, Direction::in, "/channel/0/gain")->count, 1);
    EXPECT_EQ (rowOf (snapshot, Direction::out, "/channel/0/gain")->count, 1);
}

TEST (Traffic, RepeatsAddUpAndTheLastValueWins)
{
    Traffic traffic;
    for (const double value : { 0.1, 0.2, 0.3 })
        seenFloat (traffic, Direction::in, "/channel/0/gain", value, "motion");
    const auto snapshot = look (traffic);
    const auto* row = rowOf (snapshot, Direction::in, "/channel/0/gain");
    ASSERT_NE (row, nullptr);
    EXPECT_EQ (row->count, 3);
    ASSERT_TRUE (row->lastValue);
    EXPECT_EQ (pythonFloat (*row->lastValue), 0.3);
}

TEST (Traffic, AnAddressNeverSeenHasNoRow)
{
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/azimuth", 1.0, "motion");
    EXPECT_EQ (rowOf (look (traffic), Direction::in, "/channel/0/gain"), nullptr);
}

// CountingTheBytes: worked out by hand from the OSC 1.0 spec, not from the code under test.

TEST (Traffic, AShortAddressAndAFloat)
{
    const auto value = pyFloat (0.5);
    EXPECT_EQ (oscBytes ("/a", &value), 12);   // "/a\0" 4, ",f\0" 4, float32 4
}

TEST (Traffic, AnAddressIsPaddedToAMultipleOfFour)
{
    const auto value = pyInt (3);
    EXPECT_EQ (oscBytes ("/abcd", &value), 16);   // "/abcd\0" 6 -> 8, ",i\0" 4, int32 4
}

TEST (Traffic, AnAddressOfExactlyFourCharactersStillNeedsItsNull)
{
    const auto value = pyFloat (1.0);
    EXPECT_EQ (oscBytes ("/abc", &value), 16);   // "/abc" plus the null is 5 -> 8
}

TEST (Traffic, AStringArgumentIsPaddedLikeTheAddress)
{
    const auto one = pyStr ("1");
    const auto hello = pyStr ("hello");
    EXPECT_EQ (oscBytes ("/x", &one), 12);      // the Mixer's momentary edge: "1\0" -> 4
    EXPECT_EQ (oscBytes ("/x", &hello), 16);    // "hello\0" is 6 -> 8
}

TEST (Traffic, AMessageWithoutArgumentsIsAddressAndTag) { EXPECT_EQ (oscBytes ("/a", nullptr), 8); }

TEST (Traffic, ABoolIsATypeTagAndNoData)
{
    const auto yes = pyBool (true);
    EXPECT_EQ (oscBytes ("/a", &yes), 8);
}

TEST (Traffic, SeenAddsTheBytesUpPerRow)
{
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/a", 0.5, "motion");
    seenFloat (traffic, Direction::in, "/a", 0.7, "motion");
    EXPECT_EQ (look (traffic).rows[0].bytes, 24);
}

TEST (Traffic, OscBytesAsPython)
{
    for (const auto& item : *fixture ("traffic.json")["sizes"].getArray())
    {
        const auto value = valueOf (item[1]);
        EXPECT_EQ (oscBytes (item[0].toString().toStdString(), value.get()), static_cast<juce::int64> (item[2])) << dump (item);
    }
}

// KeepingTheType: a string "1" and a float 1.0 mean different things in this rig.

TEST (Traffic, AStringIsReportedAsAString)
{
    Traffic traffic;
    const auto edge = pyStr ("1");
    traffic.seen (Direction::in, "/channel/0/pfl", &edge, "mixer", tick());
    const auto row = look (traffic).rows[0];
    ASSERT_TRUE (row.lastValue);
    EXPECT_EQ (std::get<std::string> (row.lastValue->value), "1");
    EXPECT_EQ (row.lastType, "str");
}

TEST (Traffic, AFloatIsReportedAsAFloat)
{
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/pfl", 1.0, "motion");
    const auto row = look (traffic).rows[0];
    ASSERT_TRUE (row.lastValue);
    EXPECT_EQ (pythonFloat (*row.lastValue), 1.0);
    EXPECT_EQ (row.lastType, "float");
}

// TheRingHoldsItsBound

TEST (Traffic, TheOldestFallsOutAndTheNewestIsKept)
{
    Traffic traffic (3);
    for (int i = 0; i < 5; ++i)
        seenInt (traffic, Direction::in, numbered ("/a/", i, ""), i, "motion");
    const auto history = look (traffic).history;
    ASSERT_EQ (history.size(), 3u);
    EXPECT_EQ (history[0].address, "/a/2");
    EXPECT_EQ (history[1].address, "/a/3");
    EXPECT_EQ (history[2].address, "/a/4");
}

TEST (Traffic, TheCountersAreNotBoundedByTheRing)
{
    Traffic traffic (3);
    for (int i = 0; i < 50; ++i)
        seenInt (traffic, Direction::in, "/a/0", 1, "motion");
    EXPECT_EQ (look (traffic).rows[0].count, 50);
}

// ChainedDeltasLoseAndDuplicateNothing: an entry belongs to a snapshot's copy if and only if its own
// stamp is not greater than the snapshot's. The stamps of seen() and snapshot() are handed in by the
// caller, read before the lock; Traffic makes them follow the order of the lock.

TEST (Traffic, TheUnionOfEveryDeltaIsEverythingWrittenOnceEach)
{
    constexpr int writers = 8, writesEach = 200, total = writers * writesEach;
    Traffic traffic (total + 100);
    std::atomic<int> counter { 0 };

    std::vector<std::thread> writerThreads;
    for (int w = 0; w < writers; ++w)
        writerThreads.emplace_back ([&] {
            for (int i = 0; i < writesEach; ++i)
            {
                const auto value = pyInt (++counter);
                traffic.seen (Direction::in, "/a/0", &value, "motion", steadySeconds());
            }
        });

    std::vector<int> collected;
    std::atomic<bool> stop { false };
    const auto collect = [&] (std::optional<double>& since) {
        const auto snapshot = traffic.snapshot (since, steadySeconds());
        for (const auto& entry : snapshot.history)
            collected.push_back (std::get<int32_t> (entry.value->value));
        since = snapshot.at;
    };
    std::thread consumer ([&] {
        std::optional<double> since;
        while (! stop)
            collect (since);
        collect (since);   // whatever landed between the last look and stop
    });

    for (auto& thread : writerThreads)
        thread.join();
    stop = true;
    consumer.join();

    // Sorted equality catches a gap and a duplicate alike.
    std::sort (collected.begin(), collected.end());
    std::vector<int> everything (total);
    for (int i = 0; i < total; ++i)
        everything[static_cast<size_t> (i)] = i + 1;
    EXPECT_EQ (collected, everything);
}

TEST (Traffic, TwoSnapshotsCarryWhatARateNeeds)
{
    Traffic traffic;
    const auto first = look (traffic);
    for (int i = 0; i < 10; ++i)
        seenInt (traffic, Direction::in, "/a/0", 1, "motion");
    const auto second = look (traffic);

    EXPECT_GT (second.at, first.at);
    EXPECT_EQ (second.rows[0].count, 10);
    EXPECT_TRUE (first.rows.empty());
}

TEST (Traffic, TheCutoffSplitsHistoryButLeavesTheRowsWhole)
{
    Traffic traffic;
    seenInt (traffic, Direction::in, "/a/0", 1, "motion");
    unknownFloat (traffic, "/track/3/name", 0.0);
    const auto cutoff = look (traffic).at;
    seenInt (traffic, Direction::in, "/a/0", 2, "motion");
    seenInt (traffic, Direction::in, "/a/1", 3, "motion");

    const auto sinceCutoff = traffic.snapshot (cutoff, tick());
    ASSERT_EQ (sinceCutoff.history.size(), 2u);
    EXPECT_EQ (sinceCutoff.history[0].address, "/a/0");
    EXPECT_EQ (sinceCutoff.history[1].address, "/a/1");
    EXPECT_EQ (sinceCutoff.rows[0].count, 2);
    EXPECT_EQ (sinceCutoff.unknownAddresses, 1u);

    EXPECT_EQ (look (traffic).history.size(), 3u);
}

// ManyThreadsAtOnce

TEST (Traffic, NothingIsLostWhenFourSendersWriteTogether)
{
    Traffic traffic (10);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back ([&] {
            for (int i = 0; i < 500; ++i)
                seenInt (traffic, Direction::in, "/a/0", 1, "motion");
        });
    for (auto& thread : threads)
        thread.join();
    EXPECT_EQ (look (traffic).rows[0].count, 2000);
}

TEST (Traffic, AReaderDuringAWriteStormGetsAWholeSnapshot)
{
    Traffic traffic (10);
    std::atomic<bool> stop { false };
    std::thread writer ([&] {
        while (! stop)
            seenInt (traffic, Direction::in, "/a/0", 1, "motion");
    });
    for (int i = 0; i < 50; ++i)
    {
        const auto snapshot = look (traffic);
        EXPECT_LE (snapshot.history.size(), 10u);
        // A counter a torn read left at zero is what would reveal the bug.
        for (const auto& row : snapshot.rows)
            EXPECT_GT (row.count, 0);
    }
    stop = true;
    writer.join();
}

// UnderstoodAndUnknownAreTwoTables

TEST (Traffic, AnUnknownAddressGetsNoRow)
{
    Traffic traffic;
    unknownFloat (traffic, "/track/3/name", 0.0);
    EXPECT_TRUE (look (traffic).rows.empty());
}

TEST (Traffic, TheSnapshotReportsTheSizeAndNotTheContents)
{
    Traffic traffic;
    for (int i = 0; i < 5; ++i)
        unknownFloat (traffic, numbered ("/track/", i, "/name"), i);
    unknownFloat (traffic, "/track/0/name", 1.0);
    const auto snapshot = look (traffic);
    EXPECT_EQ (snapshot.unknownAddresses, 5u);
    EXPECT_EQ (snapshot.unknownMessages, 6);
}

TEST (Traffic, TheUnknownTableIsFetchedSeparatelyAndLooksLikeRows)
{
    Traffic traffic;
    unknownFloat (traffic, "/track/3/pan", 0.25);
    const auto row = lookUnknown (traffic).rows[0];
    EXPECT_EQ (row.address, "/track/3/pan");
    ASSERT_TRUE (row.lastValue);
    EXPECT_EQ (pythonFloat (*row.lastValue), 0.25);
    EXPECT_EQ (row.lastType, "float");
    EXPECT_EQ (row.count, 1);
    EXPECT_EQ (row.peer, "reaper");
}

TEST (Traffic, UnderstoodAndUnknownDoNotMix)
{
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "motion");
    unknownFloat (traffic, "/track/3/name", 0.0);
    const auto understood = look (traffic).rows;
    ASSERT_EQ (understood.size(), 1u);
    EXPECT_EQ (understood[0].address, "/channel/0/gain");
    const auto unknown = lookUnknown (traffic).rows;
    ASSERT_EQ (unknown.size(), 1u);
    EXPECT_EQ (unknown[0].address, "/track/3/name");
}

// TheCapsEvictAndSaySo

TEST (Traffic, TheUnknownTableStopsAtItsCap)
{
    Traffic traffic (Traffic::historyCap, Traffic::rowCap, 10);
    for (int i = 0; i < 25; ++i)
        unknownFloat (traffic, numbered ("/track/", i, "/name"), 0.0);
    const auto snapshot = lookUnknown (traffic);
    EXPECT_EQ (snapshot.rows.size(), 10u);
    EXPECT_EQ (snapshot.evicted, 15);
}

TEST (Traffic, TheRowTableStopsAtItsCap)
{
    Traffic traffic (Traffic::historyCap, 10);
    for (int i = 0; i < 25; ++i)
        seenFloat (traffic, Direction::in, numbered ("/channel/", i, "/gain"), 0.0, "motion");
    const auto snapshot = look (traffic);
    EXPECT_EQ (snapshot.rows.size(), 10u);
    EXPECT_EQ (snapshot.evictedRows, 15);
}

TEST (Traffic, EvictionDropsTheLeastRecentlySeen)
{
    Traffic traffic (Traffic::historyCap, Traffic::rowCap, 3);
    for (const char* name : { "a", "b", "c" })
        unknownFloat (traffic, std::string ("/track/") + name, 0.0);
    unknownFloat (traffic, "/track/a", 1.0);   // a is touched again
    unknownFloat (traffic, "/track/d", 0.0);   // pushes one out
    std::set<std::string> left;
    for (const auto& row : lookUnknown (traffic).rows)
        left.insert (row.address);
    EXPECT_EQ (left, (std::set<std::string> { "/track/a", "/track/c", "/track/d" }));
}

TEST (Traffic, ARepeatDoesNotCountAgainstTheCap)
{
    Traffic traffic (Traffic::historyCap, Traffic::rowCap, 3);
    for (int i = 0; i < 50; ++i)
        unknownFloat (traffic, "/track/3/name", 0.0);
    const auto snapshot = lookUnknown (traffic);
    ASSERT_EQ (snapshot.rows.size(), 1u);
    EXPECT_EQ (snapshot.rows[0].count, 50);
    EXPECT_EQ (snapshot.evicted, 0);
}

TEST (Traffic, NothingIsEvictedBelowTheCap)
{
    Traffic traffic (Traffic::historyCap, Traffic::rowCap, 10);
    for (int i = 0; i < 10; ++i)
        unknownFloat (traffic, numbered ("/track/", i, ""), 0.0);
    EXPECT_EQ (lookUnknown (traffic).evicted, 0);
    EXPECT_EQ (look (traffic).evictedUnknown, 0);
}

TEST (Traffic, UnknownMessagesCountsMessagesEverSeenNotRowsStillHeld)
{
    // "N Nachrichten" on the page reads like a lifetime total: an evicted row's messages already
    // happened, and dropping the row must not un-happen them.
    Traffic traffic (Traffic::historyCap, Traffic::rowCap, 2);
    unknownFloat (traffic, "/track/a", 0.0);
    unknownFloat (traffic, "/track/b", 0.0);
    unknownFloat (traffic, "/track/c", 0.0);   // evicts /track/a
    EXPECT_EQ (lookUnknown (traffic).evicted, 1);
    EXPECT_EQ (look (traffic).unknownMessages, 3);
}

// TheRealShapeOfTheIncident: 19,335 unknown addresses against 44 understood ones.

TEST (Traffic, NineteenThousandUnknownsDoNotEnterTheSnapshot)
{
    Traffic traffic;
    for (int i = 0; i < 19335; ++i)
        unknownFloat (traffic, "/track/" + std::to_string (i / 8) + "/param/" + std::to_string (i % 8), 0.0);
    for (int i = 0; i < 44; ++i)
        seenFloat (traffic, Direction::in, numbered ("/channel/", i, "/gain"), 0.0, "motion");

    const auto snapshot = look (traffic);
    EXPECT_EQ (snapshot.rows.size(), 44u);
    EXPECT_EQ (snapshot.unknownAddresses, 19335u);
    EXPECT_LT (dumpIndented (snapshotAsJson (snapshot, nullptr), -1).size(), 64u * 1024u);
    EXPECT_EQ (lookUnknown (traffic).rows.size(), 19335u);
}

// AnAddressDoesNotStayUnknownOnceItIsUnderstood

TEST (Traffic, ARowSeenIncomingClearsItsUnknownRow)
{
    Traffic traffic;
    unknownFloat (traffic, "/track/1/fx/1/fxparam/1/value", 0.5);
    EXPECT_EQ (lookUnknown (traffic).rows.size(), 1u);

    seenFloat (traffic, Direction::in, "/track/1/fx/1/fxparam/1/value", 0.5, "reaper");
    EXPECT_TRUE (lookUnknown (traffic).rows.empty());
}

TEST (Traffic, AnOutgoingRowDoesNot)
{
    // Core sends `/track/n/fx/1/fxparam/1/value` and cannot read the crossfade back out of it: known
    // outbound, unknown inbound, and that pair is the truth.
    Traffic traffic;
    unknownFloat (traffic, "/track/10/fx/1/fxparam/1/value", 0.5);
    seenFloat (traffic, Direction::out, "/track/10/fx/1/fxparam/1/value", 0.5, "reaper");
    EXPECT_EQ (lookUnknown (traffic).rows.size(), 1u);
}

TEST (Traffic, ARestoredUnknownRowYieldsToARestoredIncomingRow)
{
    // The case the creating branch cannot reach: both tables survive a restart and what Core can route
    // does not, so the known row is already there and no message ever takes that branch again.
    Traffic traffic;
    traffic.restore ({ { Direction::in, "/track/1/fx/1/fxparam/1/value", 4, "reaper", 0.0 } },
                     { { "/track/1/fx/1/fxparam/1/value", 9, "reaper", 0.0 } }, 100.0, 50.0);

    EXPECT_TRUE (lookUnknown (traffic).rows.empty());
    EXPECT_EQ (look (traffic).rows.size(), 1u);
}

TEST (Traffic, ARestoredOutgoingRowDoesNotClearIt)
{
    Traffic traffic;
    traffic.restore ({ { Direction::out, "/track/10/fx/1/fxparam/1/value", 4, "reaper", 0.0 } },
                     { { "/track/10/fx/1/fxparam/1/value", 9, "reaper", 0.0 } }, 100.0, 50.0);

    EXPECT_EQ (lookUnknown (traffic).rows.size(), 1u);
}

TEST (Traffic, ItCostsNothingOnTheHotPath)
{
    // Only the branch that creates a row looks at the unknown table; a repeat message leaves the
    // unknown row alone. An address cannot go from routed back to unrecognised anyway.
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "mixer");
    unknownFloat (traffic, "/channel/0/gain", 0.5);

    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.6, "mixer");
    EXPECT_EQ (lookUnknown (traffic).rows.size(), 1u);
}

// Beyond B0's tests: the stamps and the restore arithmetic.

TEST (Traffic, StampsNeverTieEvenOnTheSameClockReading)
{
    // A snapshot and an entry on one reading of the clock would tie, and a tie reads as "later" to the
    // next cutoff -- the entry would never be sent. Equal readings are told apart.
    Traffic traffic;
    const auto value = pyInt (1);
    traffic.seen (Direction::in, "/a", &value, "motion", 5.0);
    const auto first = traffic.snapshot (std::nullopt, 5.0);
    traffic.seen (Direction::in, "/a", &value, "motion", 5.0);
    const auto delta = traffic.snapshot (first.at, 5.0);
    EXPECT_GE (first.at, 5.0);
    EXPECT_EQ (delta.history.size(), 1u);
    EXPECT_GT (delta.at, first.at);
}

TEST (Traffic, ARestoredRowIsAsOldAsItsFileSaysAndNeverFromTheFuture)
{
    Traffic traffic;
    traffic.restore ({ { Direction::in, "/a", 1, "m", 900.0 }, { Direction::in, "/b", 1, "m", 1100.0 } }, {}, 1000.0, 50.0);
    const auto snapshot = traffic.snapshot (std::nullopt, 60.0);
    EXPECT_EQ (rowOf (snapshot, Direction::in, "/a")->lastSeen, -50.0);
    EXPECT_EQ (rowOf (snapshot, Direction::in, "/b")->lastSeen, 50.0);
    EXPECT_EQ (traffic.created(), 2);
}

// The serialiser a3_core_web.as_json: recorded for a scripted traffic, JSON-equal.

TEST (Traffic, SnapshotAsPython)
{
    const auto& recorded = fixture ("traffic.json")["as_json"];
    Traffic traffic;
    play (traffic, recorded["first_ops"]);
    const auto one = traffic.snapshot (std::nullopt, static_cast<double> (recorded["looks"]["first"]["t"]));
    expectJsonEqual (recorded["looks"]["first"]["json"], snapshotAsJson (one, nullptr), "first look");

    play (traffic, recorded["second_ops"]);
    const auto& delta = recorded["looks"]["second_delta"];
    const auto two = traffic.snapshot (static_cast<double> (delta["since"]), static_cast<double> (delta["t"]));
    ASSERT_EQ (two.at, static_cast<double> (delta["t"]));
    expectJsonEqual (delta["json"], snapshotAsJson (two, &one), "the delta against the first");

    const auto three = traffic.snapshot (std::nullopt, static_cast<double> (recorded["looks"]["third_full"]["t"]));
    expectJsonEqual (recorded["looks"]["third_full"]["json"], snapshotAsJson (three, nullptr), "the whole ring again");

    const auto unknown = traffic.unknownSnapshot (static_cast<double> (recorded["unknown_look"]["t"]));
    expectJsonEqual (recorded["unknown_look"]["json"], unknownAsJson (unknown), "the unknown table");
}

TEST (Traffic, ARowCapOfZeroKeepsTheHistory)
{
    // The row is evicted the moment it is made; the history entry must not be read back from it.
    Traffic traffic (Traffic::historyCap, 0);
    const auto value = pyStr ("a string long enough to live on the heap, not in the string's buffer");
    traffic.seen (Direction::in, "/a/long/enough/to/live/on/the/heap/too", &value, "motion", tick());
    const auto snapshot = look (traffic);
    EXPECT_TRUE (snapshot.rows.empty());
    ASSERT_EQ (snapshot.history.size(), 1u);
    EXPECT_EQ (snapshot.history[0].type, "str");
    ASSERT_TRUE (snapshot.history[0].value);
    EXPECT_EQ (std::get<std::string> (snapshot.history[0].value->value), std::get<std::string> (value.value));
}
