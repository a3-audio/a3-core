// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of the module cases of a3-core's tools/tests/test_core_devices.py (B0 f3345dc7feeb); the
// window, source-text and truth-file cases belong to later tasks.
#include <gtest/gtest.h>

#include "Devices.h"
#include "Json.h"

using namespace a3core;

namespace
{
const juce::File truthFile = juce::File (A3_CORE_TEST_DATA).getChildFile ("../../engine/data/a3-osc.json");
}

TEST (Devices, ItIsTheSha256OfTheFile)
{
    ASSERT_TRUE (truthFile.existsAsFile());
    juce::MemoryBlock bytes;
    truthFile.loadFileAsData (bytes);
    EXPECT_EQ (truthHash (truthFile), sha256Hex (std::string (static_cast<const char*> (bytes.getData()), bytes.getSize())));
}

TEST (Devices, OneChangedByteIsAnotherHash)
{
    juce::TemporaryFile copy (".json");
    juce::MemoryBlock bytes;
    truthFile.loadFileAsData (bytes);
    bytes.append (" ", 1);
    ASSERT_TRUE (copy.getFile().replaceWithData (bytes.getData(), bytes.getSize()));
    EXPECT_NE (truthHash (copy.getFile()), truthHash (truthFile));
}

namespace
{
juce::var onlyRow (const Devices& devices, double now)
{
    const auto rows = devices.snapshot (now);
    EXPECT_EQ (rows.size(), 1);
    return rows[0];
}
}

TEST (Devices, ADeviceOnTheSameTruthMatches)
{
    Devices devices ("abc");
    devices.heard ("mixer", "abc", 100.0);
    const auto row = onlyRow (devices, 103.0);
    EXPECT_EQ (row["device"].toString(), "mixer");
    EXPECT_EQ (row["hash"].toString(), "abc");
    EXPECT_TRUE ((bool) row["matches"]);
    EXPECT_EQ ((double) row["age"], 3.0);
}

TEST (Devices, ADeviceOnAnotherTruthDoesNot)
{
    Devices devices ("abc");
    devices.heard ("mixer", "old", 100.0);
    EXPECT_FALSE ((bool) onlyRow (devices, 100.0)["matches"]);
}

TEST (Devices, TheLastWordCounts)
{
    Devices devices ("abc");
    devices.heard ("mixer", "old", 100.0);
    devices.heard ("mixer", "abc", 200.0);
    EXPECT_TRUE ((bool) onlyRow (devices, 200.0)["matches"]);
}

TEST (Devices, ItSaysWhenADeviceChangedItsMind)
{
    Devices devices ("abc");
    EXPECT_TRUE (devices.heard ("mixer", "old", 1.0)) << "the first word is news";
    EXPECT_FALSE (devices.heard ("mixer", "old", 2.0)) << "the same word again is not";
    EXPECT_TRUE (devices.heard ("mixer", "abc", 3.0));
}
