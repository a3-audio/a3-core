// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of the TheFileItself cases of a3-core's tools/tests/test_core_state.py (B0 f3345dc7feeb), on
// a clock the test turns: the debounce is a deadline Core's tick checks (no timer thread, P8).
// Plus the rig's real files, recorded by tools/parity/record_state.py.
#include <gtest/gtest.h>

#include "StateRecorded.h"

using namespace a3core;

namespace
{
juce::var state (int mode)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("fx_mode", mode);
    return juce::var (object);
}

std::vector<std::string> namesIn (const juce::File& directory)
{
    std::vector<std::string> names;
    for (const auto& entry : juce::RangedDirectoryIterator (directory, false))
        names.push_back (entry.getFile().getFileName().toStdString());
    return names;
}

}

// TheFileItself

TEST (StateFile, AFileThatIsNotThereIsAnEmptyState)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    EXPECT_EQ (dumpIndented (StateFile (path).load(), 2), "{}");
}

TEST (StateFile, AFileThatIsNotJsonIsAnEmptyState)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    recorded::writeBytes (path, "{ this is not");
    EXPECT_EQ (dumpIndented (StateFile (path).load(), 2), "{}");
}

TEST (StateFile, WhatWasWrittenIsWhatIsRead)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    StateFile file (path, 0.01);
    file.remember (state (1), 0.0);
    file.flush();
    EXPECT_EQ (dumpIndented (StateFile (path).load(), 2), dumpIndented (state (1), 2));
}

TEST (StateFile, TheDirectoryIsMadeIfItIsNotThere)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    const auto deeper = temp.dir.getChildFile ("deeper").getChildFile ("state.json");
    StateFile file (deeper, 0.01);
    file.remember (state (1), 0.0);
    file.flush();
    EXPECT_TRUE (deeper.existsAsFile());
}

TEST (StateFile, NothingIsLeftBesideTheFile)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    StateFile file (path, 0.01);
    file.remember (state (1), 0.0);
    file.flush();
    EXPECT_EQ (namesIn (temp.dir), std::vector<std::string> { "state.json" });
}

TEST (StateFile, ABurstOfChangesIsOneWrite)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    StateFile file (path, 0.05);
    for (int value = 0; value < 20; ++value)
        file.remember (state (value), 0.001 * value);

    file.flushIfDue (0.019 + 0.049);
    EXPECT_EQ (file.writes(), 0);   // the clock restarted with the last change
    EXPECT_FALSE (path.exists());

    file.flushIfDue (0.019 + 0.05);
    file.flushIfDue (1.0);
    EXPECT_EQ (file.writes(), 1);
    EXPECT_EQ (dumpIndented (StateFile (path).load(), 2), dumpIndented (state (19), 2));
}

TEST (StateFile, TheSameStateAgainDoesNotRestartTheClock)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    StateFile file (path, 0.05);
    file.remember (state (1), 0.0);
    for (int sweep = 1; sweep <= 50; ++sweep)
        file.remember (state (1), 0.01 * sweep);   // half a second of sweeping that changes nothing

    file.flushIfDue (0.05);   // due since the one change, not since the last offer
    EXPECT_TRUE (path.existsAsFile());
    EXPECT_EQ (file.writes(), 1);
}

TEST (StateFile, FlushWritesNothingWhenNothingChanged)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    StateFile file (path, 0.01);
    file.flush();
    file.flushIfDue (100.0);
    EXPECT_EQ (file.writes(), 0);
    EXPECT_FALSE (path.exists());
}

// A state already written is not a change, and a mutated state is.

TEST (StateFile, AStateChangedInPlaceIsAChange)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    StateFile file (path, 0.01);
    auto* object = new juce::DynamicObject();
    object->setProperty ("fx_mode", 0);
    juce::var offered (object);

    file.remember (offered, 0.0);
    file.flush();
    object->setProperty ("fx_mode", 1);   // the same object, as a rig's list is
    file.remember (offered, 1.0);
    file.flush();
    EXPECT_EQ (file.writes(), 2);
}

TEST (StateFile, TheSameWordsInAnotherOrderAreTheSameState)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    StateFile file (path, 0.01);
    juce::String error;
    file.remember (parseJson ("{\"a\": 1, \"b\": [1, 2.0]}", error), 0.0);
    file.flush();
    file.remember (parseJson ("{\"b\": [1.0, 2], \"a\": true}", error), 1.0);   // Python: 1 == 1.0 == True
    file.flush();
    EXPECT_EQ (file.writes(), 1);
}

TEST (StateFile, AWriteThatCannotBeMadeDoesNotThrow)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("state.json");
    recorded::writeBytes (temp.dir.getChildFile ("blocker"), "a file where a directory is due");
    StateFile file (temp.dir.getChildFile ("blocker").getChildFile ("state.json"), 0.01);
    file.remember (state (1), 0.0);
    EXPECT_NO_THROW (file.flush());
    EXPECT_EQ (file.writes(), 0);
}

// The rig's real files

TEST (StateFile, HalfWrittenIsEmpty)
{
    const auto cases = recorded::load ("state.json");
    const auto whole = recorded::text (cases["state-current"]["text"]);

    EXPECT_EQ (dumpIndented (recorded::loadedFrom (recorded::text (cases["state-half"]["text"])), 2), "{}");
    EXPECT_EQ (dumpIndented (recorded::loadedFrom (""), 2), "{}");
    const auto closed = whole.find_last_not_of ("\n") + 1;   // up to the closing brace: that much is a whole file
    for (size_t length = 0; length < closed; ++length)   // a crash can stop after any byte
        ASSERT_EQ (dumpIndented (recorded::loadedFrom (whole.substr (0, length)), 2), "{}") << length;
}

TEST (StateFile, ATextThatIsNoUtf8IsAnEmptyState)
{
    EXPECT_EQ (dumpIndented (recorded::loadedFrom ("{\"fx_mode\": \"\xff\"}"), 2), "{}");
    EXPECT_EQ (dumpIndented (recorded::loadedFrom ("\xef\xbb\xbf{\"fx_mode\": 1}"), 2), "{}");   // Python refuses a BOM
}

TEST (StateFile, WrittenFileReadsBackInPython)
{
    // The bytes B0's StateFile wrote for the same state: json.dumps(indent=2) plus a newline, so
    // the Python Core reads our file as its own.
    const auto cases = recorded::load ("state.json");
    int compared = 0;
    for (const auto& [name, entry] : cases.getDynamicObject()->getProperties())
    {
        if (! entry.hasProperty ("file_written"))
            continue;
        recorded::TempDir temp;
        const auto file = temp.dir.getChildFile ("state.json");
        StateFile writer (file, 0.0);
        writer.remember (recorded::loadedFrom (recorded::text (entry["text"])), 0.0);
        writer.flush();
        EXPECT_EQ (recorded::readBytes (file), recorded::text (entry["file_written"])) << name.toString();
        EXPECT_EQ (namesIn (temp.dir), std::vector<std::string> { "state.json" });
        ++compared;
    }
    EXPECT_GE (compared, 5);
}

// A NaN held (B1 a3-core 7a7b2441f74e): Python's == between two states that hold the same float object
// is True even for a NaN (a container compares by identity first). Core's state holds a NaN 3D or
// phones-mix as one object until a message assigns a new one: a NaN kept is no change, a NaN that
// arrives anew (freshNan) is.

TEST (StateFile, TheSameNanKeptIsNoChange)
{
    recorded::TempDir temp;
    StateFile file (temp.dir.getChildFile ("state.json"), 1.0);
    juce::String error;
    file.remember (parseJson ("{\"three_d\": NaN, \"a\": 1}", error), 0.0);
    file.remember (parseJson ("{\"three_d\": NaN, \"a\": 1}", error), 0.5);
    file.flushIfDue (1.0);   // due since the one change
    EXPECT_EQ (file.writes(), 1);
}

TEST (StateFile, AFreshNanIsAChange)
{
    recorded::TempDir temp;
    StateFile file (temp.dir.getChildFile ("state.json"), 1.0);
    juce::String error;
    file.remember (parseJson ("{\"three_d\": NaN}", error), 0.0);
    file.remember (parseJson ("{\"three_d\": NaN}", error), 0.5, true);
    file.flushIfDue (1.0);
    EXPECT_EQ (file.writes(), 0);
    file.flushIfDue (1.5);
    EXPECT_EQ (file.writes(), 1);
}
