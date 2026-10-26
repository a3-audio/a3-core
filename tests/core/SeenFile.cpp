// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_seen.py (B0 f3345dc7feeb): the address list, kept across a
// restart. Debounce is a deadline the serve thread's tick checks (no timer thread, P8), so the tests
// turn a clock instead of sleeping. Plus the file Python wrote, recorded by tools/parity/record_traffic.py.
#include <cstdlib>

#include <gtest/gtest.h>

#include "StateRecorded.h"
#include "TrafficRecorded.h"
#include "SeenFile.h"

using namespace a3core;
using namespace recordedtraffic;

namespace
{
constexpr double wallStart = 1'800'000'000.0;

struct Folder
{
    recorded::TempDir temp;
    juce::File path = temp.dir.getChildFile ("seen.json");
    ScriptedClock clock;
    Folder()
    {
        clock.wallTime = wallStart;
        clock.mono = 100.0;
    }
    SeenFile fileFor (Traffic& traffic, double delay = SeenFile::defaultDelay) { return SeenFile (path, traffic, delay, &clock); }
};

std::string textOf (const juce::File& file) { return recorded::readBytes (file); }

juce::var parsedText (const std::string& text)
{
    juce::String error;
    auto value = parseJson (juce::String::fromUTF8 (text.data(), static_cast<int> (text.size())), error);
    EXPECT_EQ (error, juce::String());
    return value;
}

void writeJson (const juce::File& file, const juce::var& value) { recorded::writeBytes (file, dumpIndented (value, -1)); }

void saved (Folder& folder)
{
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "mixer");
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.6, "mixer");
    seenFloat (traffic, Direction::out, "/track/12/volume", 0.4, "reaper");
    unknownOf (traffic, "/track/3/name", pyStr ("kick"));
    folder.fileFor (traffic).write();
}

// Turn the clock by `seconds` in steps, ticking the file as the serve thread would.
void runFor (SeenFile& file, ScriptedClock& clock, double seconds, double step = 0.01)
{
    for (double elapsed = 0; elapsed < seconds; elapsed += step)
    {
        clock.mono += step;
        file.writeIfDue (clock.mono);
    }
}

bool runUntilWritten (SeenFile& file, ScriptedClock& clock, int writes, double limit = 5.0)
{
    for (double elapsed = 0; elapsed < limit; elapsed += 0.01)
    {
        if (file.writes >= writes)
            return true;
        clock.mono += 0.01;
        file.writeIfDue (clock.mono);
    }
    return file.writes >= writes;
}

TrafficSnapshot lookAt (Traffic& traffic, double now) { return traffic.snapshot (std::nullopt, now); }

std::vector<std::string> addresses (const TrafficSnapshot& snapshot)
{
    std::vector<std::string> out;
    for (const auto& row : snapshot.rows)
        out.push_back (row.address);
    std::sort (out.begin(), out.end());
    return out;
}
}

// WhereItLives

TEST (SeenFile, ItFollowsXdgStateHome)
{
    const char* before = std::getenv ("XDG_STATE_HOME");
    const std::string kept = before != nullptr ? before : "";
    setenv ("XDG_STATE_HOME", "/tmp/somewhere", 1);
    EXPECT_EQ (seenPath (stateDirectory()).getFullPathName().toStdString(), "/tmp/somewhere/a3-core/seen.json");
    if (before != nullptr)
        setenv ("XDG_STATE_HOME", kept.c_str(), 1);
    else
        unsetenv ("XDG_STATE_HOME");
}

TEST (SeenFile, WithoutItTheDefaultIsUnderHome)
{
    const char* before = std::getenv ("XDG_STATE_HOME");
    const std::string kept = before != nullptr ? before : "";
    unsetenv ("XDG_STATE_HOME");
    EXPECT_EQ (seenPath (stateDirectory()),
               juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/state/a3-core/seen.json"));
    if (before != nullptr)
        setenv ("XDG_STATE_HOME", kept.c_str(), 1);
}

TEST (SeenFile, TheStateDirectoryIsASeamNotAGlobal)
{
    const juce::File home ("/home/someone");
    EXPECT_EQ (stateDirectory (std::string ("/var/state"), home), juce::File ("/var/state"));
    EXPECT_EQ (stateDirectory (std::nullopt, home), home.getChildFile (".local/state"));
}

// ItComesBack

TEST (SeenFile, TheUnderstoodAddressesComeBack)
{
    Folder folder;
    saved (folder);
    Traffic after;
    folder.fileFor (after).restore();
    const auto snapshot = lookAt (after, 100.0);
    const auto* gain = rowOf (snapshot, Direction::in, "/channel/0/gain");
    ASSERT_NE (gain, nullptr);
    EXPECT_EQ (gain->count, 2);
    EXPECT_EQ (gain->peer, "mixer");
    EXPECT_NE (rowOf (snapshot, Direction::out, "/track/12/volume"), nullptr);
}

TEST (SeenFile, TheUnknownAddressesComeBack)
{
    Folder folder;
    saved (folder);
    Traffic after;
    folder.fileFor (after).restore();
    const auto rows = after.unknownSnapshot (100.0).rows;
    ASSERT_EQ (rows.size(), 1u);
    EXPECT_EQ (rows[0].address, "/track/3/name");
    EXPECT_EQ (lookAt (after, 100.0).unknownAddresses, 1u);
}

TEST (SeenFile, TheHistoryDoesNotComeBack)
{
    // Two minutes of what just happened, and none of it happened in this process.
    Folder folder;
    saved (folder);
    Traffic after;
    folder.fileFor (after).restore();
    EXPECT_TRUE (lookAt (after, 100.0).history.empty());
}

TEST (SeenFile, TheLastValueDoesNotComeBack)
{
    // It was true before the restart and says nothing about now; the recall answers that.
    Folder folder;
    saved (folder);
    Traffic after;
    folder.fileFor (after).restore();
    const auto row = lookAt (after, 100.0).rows[0];
    EXPECT_FALSE (row.lastValue);
    EXPECT_EQ (row.lastType, "");
}

TEST (SeenFile, AnOldAddressReadsAsOld)
{
    // The age column tells a live wire from a dead one, so a restored row must not read as "gerade".
    Folder folder;
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "mixer");
    folder.fileFor (traffic).write();

    // Rewrite the file as though it had been saved an hour ago.
    auto file = parsedText (textOf (folder.path));
    file.getDynamicObject()->setProperty ("at", static_cast<double> (file["at"]) - 3600.0);
    for (auto& row : *file["rows"].getArray())
        row.getArray()->set (4, static_cast<double> (row[4]) - 3600.0);
    writeJson (folder.path, file);

    Traffic after;
    folder.fileFor (after).restore();
    const auto snapshot = lookAt (after, folder.clock.mono);
    EXPECT_GT (snapshot.at - snapshot.rows[0].lastSeen, 3500.0);
}

TEST (SeenFile, AClockThatWentBackwardsDoesNotMakeARowFromTheFuture)
{
    Folder folder;
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "mixer");
    folder.fileFor (traffic).write();

    auto file = parsedText (textOf (folder.path));
    file.getDynamicObject()->setProperty ("at", static_cast<double> (file["at"]) + 86400.0);   // from tomorrow
    for (auto& row : *file["rows"].getArray())
        row.getArray()->set (4, static_cast<double> (row[4]) + 86400.0);   // and so are its rows
    writeJson (folder.path, file);

    Traffic after;
    folder.fileFor (after).restore();
    const auto snapshot = lookAt (after, folder.clock.mono);
    EXPECT_GE (snapshot.at - snapshot.rows[0].lastSeen, 0.0);
}

TEST (SeenFile, ALiveRowWinsOverASavedOne)
{
    // Restoring happens at start-up, but nothing stops a message arriving first.
    Folder folder;
    saved (folder);
    Traffic after;
    seenFloat (after, Direction::in, "/channel/0/gain", 0.9, "motion");
    folder.fileFor (after).restore();
    const auto snapshot = lookAt (after, 100.0);
    const auto* row = rowOf (snapshot, Direction::in, "/channel/0/gain");
    ASSERT_NE (row, nullptr);
    EXPECT_EQ (row->count, 1);
    EXPECT_EQ (row->peer, "motion");
}

// NothingOnTheHotPath

TEST (SeenFile, SeeingAMessageWritesNothing)
{
    // `seen` runs about a hundred times a second: a file in there is a file in the way of the music.
    Folder folder;
    Traffic traffic;
    auto file = folder.fileFor (traffic, 60.0);
    runFor (file, folder.clock, 1.0);
    for (int i = 0; i < 500; ++i)
        seenFloat (traffic, Direction::in, "/channel/" + std::to_string (i) + "/gain", 0.5, "mixer");
    EXPECT_EQ (file.writes, 0);
    EXPECT_FALSE (folder.path.existsAsFile());
}

TEST (SeenFile, ABurstOfNewAddressesIsOneWrite)
{
    Folder folder;
    Traffic traffic;
    auto file = folder.fileFor (traffic, 0.05);
    file.writeIfDue (folder.clock.mono);
    for (int i = 0; i < 200; ++i)
    {
        seenFloat (traffic, Direction::in, "/channel/" + std::to_string (i) + "/gain", 0.5, "mixer");
        folder.clock.mono += 0.001;
        file.writeIfDue (folder.clock.mono);
    }
    ASSERT_TRUE (runUntilWritten (file, folder.clock, 1));
    runFor (file, folder.clock, 0.2);
    EXPECT_EQ (file.writes, 1);
}

TEST (SeenFile, MessagesOnAnAddressAlreadyKnownWriteNothing)
{
    // The list is what is kept: a count that ticks up on an address already in the file is no new list.
    Folder folder;
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "mixer");
    auto file = folder.fileFor (traffic, 0.05);
    ASSERT_TRUE (runUntilWritten (file, folder.clock, 1));
    for (int i = 0; i < 1000; ++i)
        seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "mixer");
    runFor (file, folder.clock, 0.25);
    EXPECT_EQ (file.writes, 1);
}

TEST (SeenFile, AWriteLeavesNoHalfFileBehind)
{
    Folder folder;
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "mixer");
    folder.fileFor (traffic).write();
    EXPECT_TRUE (folder.path.existsAsFile());
    EXPECT_FALSE (folder.path.getSiblingFile ("seen.json.new").exists());
}

// ABrokenFileIsNotAReasonToFall

TEST (SeenFile, AMissingFileRestoresNothingAndRaisesNothing)
{
    Folder folder;
    Traffic traffic;
    EXPECT_EQ (folder.fileFor (traffic).restore(), 0);
    EXPECT_TRUE (lookAt (traffic, 100.0).rows.empty());
}

TEST (SeenFile, HalfAFileRestoresNothing)
{
    Folder folder;
    recorded::writeBytes (folder.path, "{\"rows\": [[\"in\", \"/a\", 1");
    Traffic traffic;
    folder.fileFor (traffic).restore();
    EXPECT_TRUE (lookAt (traffic, 100.0).rows.empty());
}

TEST (SeenFile, AFileOfAnotherShapeRestoresNothing)
{
    Folder folder;
    recorded::writeBytes (folder.path, "\"a string where an object should be\"");
    Traffic traffic;
    folder.fileFor (traffic).restore();
    EXPECT_TRUE (lookAt (traffic, 100.0).rows.empty());
}

TEST (SeenFile, OneBadRowDoesNotTakeTheGoodOnesWithIt)
{
    Folder folder;
    const auto now = std::to_string (folder.clock.wallTime);
    recorded::writeBytes (folder.path,
                          "{\"version\": 1, \"at\": " + now + ", \"rows\": ["
                          "[\"in\", \"/a\", 1, \"mixer\", " + now + "], [\"in\"], \"not a row at all\","
                          "[\"in\", \"/b\", \"not a number\", \"mixer\", " + now + "],"
                          "[\"out\", \"/c\", 2, \"reaper\", " + now + "]], \"unknown\": []}");
    Traffic traffic;
    folder.fileFor (traffic).restore();
    EXPECT_EQ (addresses (lookAt (traffic, 100.0)), (std::vector<std::string> { "/a", "/c" }));
}

TEST (SeenFile, ADirectoryThatCannotBeWrittenDoesNotRaise)
{
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/0/gain", 0.5, "mixer");
    ScriptedClock clock;
    SeenFile file (juce::File ("/proc/nowhere/seen.json"), traffic, SeenFile::defaultDelay, &clock);
    file.write();
    EXPECT_EQ (file.writes, 0);
    EXPECT_FALSE (file.problem.empty());
}

// Task 9's own pins

TEST (SeenFile, OtherVersionRestoresNothing)
{
    // The first start after the cut meets a seen.json of another version: an empty list, never a crash.
    Folder folder;
    recorded::writeBytes (folder.path, "{\"version\": 2, \"at\": 1.0, \"rows\": [[\"in\", \"/a\", 1, \"m\", 1.0]],"
                                       "\"unknown\": [[\"/u\", 1, \"r\", 1.0]]}");
    Traffic traffic;
    EXPECT_EQ (folder.fileFor (traffic).restore(), 0);
    EXPECT_TRUE (lookAt (traffic, 100.0).rows.empty());
    EXPECT_TRUE (traffic.unknownSnapshot (100.0).rows.empty());
    EXPECT_EQ (traffic.created(), 0);
}

TEST (SeenFile, ReadsThePythonFile)
{
    const auto& recordedSeen = fixture ("seen.json");
    const auto& written = recordedSeen["restored"];
    Folder folder;
    recorded::writeBytes (folder.path, recordedSeen["written_by_python"]["text"].toString().toStdString());
    folder.clock.wallTime = static_cast<double> (written["wall"]);
    folder.clock.mono = static_cast<double> (written["mono_at_restore"]);

    Traffic traffic;
    EXPECT_EQ (folder.fileFor (traffic).restore(), static_cast<int> (written["put"]));

    const auto snapshot = lookAt (traffic, static_cast<double> (written["snapshot_at"]));
    const auto& rows = *written["rows"].getArray();
    ASSERT_EQ (snapshot.rows.size(), rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        EXPECT_EQ (directionName (snapshot.rows[i].direction), rows[i][0].toString().toStdString());
        EXPECT_EQ (snapshot.rows[i].address, rows[i][1].toString().toStdString());
        EXPECT_EQ (snapshot.rows[i].count, static_cast<juce::int64> (rows[i][2]));
        EXPECT_EQ (snapshot.rows[i].peer, rows[i][3].toString().toStdString());
        EXPECT_EQ (snapshot.rows[i].lastSeen, static_cast<double> (rows[i][4]));
        EXPECT_EQ (snapshot.rows[i].bytes, static_cast<juce::int64> (rows[i][5]));
    }
    const auto unknown = traffic.unknownSnapshot (static_cast<double> (written["snapshot_at"])).rows;
    const auto& expectedUnknown = *written["unknown"].getArray();
    ASSERT_EQ (unknown.size(), expectedUnknown.size());
    for (size_t i = 0; i < unknown.size(); ++i)
    {
        EXPECT_EQ (unknown[i].address, expectedUnknown[i][0].toString().toStdString());
        EXPECT_EQ (unknown[i].count, static_cast<juce::int64> (expectedUnknown[i][1]));
        EXPECT_EQ (unknown[i].peer, expectedUnknown[i][2].toString().toStdString());
        EXPECT_EQ (unknown[i].lastSeen, static_cast<double> (expectedUnknown[i][3]));
    }
    EXPECT_EQ (snapshot.unknownMessages, static_cast<juce::int64> (written["unknown_messages"]));
    EXPECT_EQ (traffic.created(), static_cast<juce::int64> (written["created"]));
}

TEST (SeenFile, EveryShapeOfFileComesBackAsInPython)
{
    const auto& shapes = *fixture ("seen.json")["shapes"].getDynamicObject();
    for (const auto& property : shapes.getProperties())
    {
        const auto name = property.name.toString().toStdString();
        const auto& expected = property.value;
        Folder folder;
        folder.clock.mono = 70.0;
        recorded::writeBytes (folder.path, expected["text"].toString().toStdString());
        Traffic traffic;
        EXPECT_EQ (folder.fileFor (traffic).restore(), static_cast<int> (expected["put"])) << name;

        const auto snapshot = lookAt (traffic, 75.0);
        const auto& rows = *expected["rows"].getArray();
        ASSERT_EQ (snapshot.rows.size(), rows.size()) << name;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            EXPECT_EQ (directionName (snapshot.rows[i].direction), rows[i][0].toString().toStdString()) << name;
            EXPECT_EQ (snapshot.rows[i].address, rows[i][1].toString().toStdString()) << name;
            EXPECT_EQ (static_cast<double> (snapshot.rows[i].count), *numberOf (rows[i][2])) << name;   // True is 1
            EXPECT_EQ (snapshot.rows[i].peer, rows[i][3].toString().toStdString()) << name;
            EXPECT_EQ (snapshot.rows[i].lastSeen, static_cast<double> (rows[i][4])) << name;
        }
        const auto unknown = traffic.unknownSnapshot (75.0).rows;
        const auto& expectedUnknown = *expected["unknown"].getArray();
        ASSERT_EQ (unknown.size(), expectedUnknown.size()) << name;
        for (size_t i = 0; i < unknown.size(); ++i)
        {
            EXPECT_EQ (unknown[i].address, expectedUnknown[i][0].toString().toStdString()) << name;
            EXPECT_EQ (unknown[i].count, static_cast<juce::int64> (expectedUnknown[i][1])) << name;
            EXPECT_EQ (unknown[i].lastSeen, static_cast<double> (expectedUnknown[i][3])) << name;
        }
        EXPECT_EQ (snapshot.unknownMessages, static_cast<juce::int64> (expected["unknown_messages"])) << name;
    }
}

TEST (SeenFile, WritesWhatPythonWrites)
{
    // The same traffic as the recorder's, written at the same clocks: JSON-equal to the file Python wrote.
    Folder folder;
    Traffic traffic;
    const auto put = [&] (Direction direction, const char* address, const OscArg& value, const char* peer, double at) {
        traffic.seen (direction, address, &value, peer, at);
    };
    put (Direction::in, "/channel/0/gain", pyFloat (0.5), "mixer", 100.0);
    put (Direction::in, "/channel/0/gain", pyFloat (0.6), "mixer", 100.5);
    put (Direction::out, "/track/12/volume", pyFloat (0.4), "reaper", 101.0);
    put (Direction::in, "/caf\xc3\xa9/\xe2\x99\xaa", pyStr ("x"), "mixer", 101.5);
    const auto kick = pyStr ("kick"), snare = pyStr ("snare");
    traffic.unknown ("/track/3/name", &kick, "reaper", 102.0);
    traffic.unknown ("/track/3/name", &kick, "reaper", 102.25);
    traffic.unknown ("/track/4/name", &snare, "reaper", 102.5);

    folder.clock.mono = 110.0;
    auto file = folder.fileFor (traffic);
    file.write();
    EXPECT_EQ (file.writes, 1);
    EXPECT_EQ (file.problem, "");
    expectJsonEqual (parsedText (fixture ("seen.json")["written_by_python"]["text"].toString().toStdString()),
                     parsedText (textOf (folder.path)), "seen.json");
}

TEST (SeenFile, AWriteAfterAFailureClearsTheProblem)
{
    Folder folder;
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/a", 0.5, "mixer");
    folder.path = juce::File ("/proc/nowhere/seen.json");
    auto file = folder.fileFor (traffic);
    file.write();
    EXPECT_FALSE (file.problem.empty());

    recorded::TempDir temp;
    SeenFile again (temp.dir.getChildFile ("deeper/seen.json"), traffic, SeenFile::defaultDelay, &folder.clock);
    again.problem = "left over";
    again.write();
    EXPECT_EQ (again.problem, "");
    EXPECT_EQ (again.writes, 1);
}
