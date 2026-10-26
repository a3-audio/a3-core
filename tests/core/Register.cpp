// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_register.py (B0 f3345dc7feeb): the register as Core serves it,
// the catalogue held against the traffic. Plus the matrices tools/parity/record_traffic.py recorded from
// B0's matcher and collapse, and B0's shipped osc-register.json.
#include <gtest/gtest.h>

#include "Register.h"
#include "StateRecorded.h"
#include "TrafficRecorded.h"

using namespace a3core;
using namespace a3core::reg;
using namespace recordedtraffic;

namespace
{
juce::var object (std::initializer_list<std::pair<const char*, juce::var>> members)
{
    auto* made = new juce::DynamicObject();
    for (const auto& member : members)
        made->setProperty (juce::Identifier (member.first), member.second);
    return juce::var (made);
}

juce::var array (std::initializer_list<juce::var> items) { return juce::var (juce::Array<juce::var> (items)); }

juce::var parsed (const juce::String& text)
{
    juce::String error;
    auto value = parseJson (text, error);
    EXPECT_EQ (error, juce::String());
    return value;
}

juce::var loadText (const std::string& text)
{
    recorded::TempDir temp;
    const auto file = temp.dir.getChildFile ("osc-register.json");
    recorded::writeBytes (file, text);
    return load (file);
}

// Written apart from the slash: a slash and a star in one literal read as the start of a comment to the
// parity guard, which would then lose the tests that follow it.
const std::string star = "*";

bool matches (const std::string& pattern, const std::string& address) { return matcher (pattern) (address); }

Counts countsFor (std::initializer_list<std::pair<std::string, long long>> rows) { return countsOf (rows); }

juce::var theRegister()
{
    return object ({ { "_comment", "c" },
                     { "devices", array ({ "core", "mixer" }) },
                     { "directions", array ({ "in", "out" }) },
                     { "entries", array ({ object ({ { "address", "/channel/{ch}/gain" }, { "device", "mixer" }, { "direction", "in" },
                                                     { "source", "a3-mixer.py:62" }, { "note", "" } }),
                                           object ({ { "address", "/channel/{ch}/4d" }, { "device", "core" }, { "direction", "in" },
                                                     { "source", "a3-core.py:577" }, { "note", "" } }) }) } });
}

juce::var entryEndingIn (const juce::var& result, const juce::String& ending)
{
    for (const auto& entry : *result["entries"].getArray())
        if (entry["address"].toString().endsWith (ending))
            return entry;
    return {};
}
}

// OneAddressStandsForAllItsNumbers

TEST (Register, ANumberedSegmentBecomesOneNumber) { EXPECT_EQ (collapse ("/track/17/volume"), "/track/0/volume"); }

TEST (Register, EveryNumberedSegmentCollapses)
{
    EXPECT_EQ (collapse ("/track/12/fx/3/fxparam/29/value"), "/track/0/fx/0/fxparam/0/value");
}

TEST (Register, AWordIsNotANumber) { EXPECT_EQ (collapse ("/master/phones_mix"), "/master/phones_mix"); }

TEST (Register, ANumberInsideAWordIsLeftAlone)
{
    // /MultiEncoder/azimuth0 is one segment and not a number: the pattern side has to cope instead.
    EXPECT_EQ (collapse ("/MultiEncoder/azimuth0"), "/MultiEncoder/azimuth0");
}

TEST (Register, CountsOfOneShapeAreAddedUp)
{
    const auto counts = countsFor ({ { "/track/1/volume", 10 }, { "/track/2/volume", 5 }, { "/master/volume", 2 } });
    EXPECT_EQ (counts.at ("/track/0/volume"), 15);
    EXPECT_EQ (counts.at ("/master/volume"), 2);
}

TEST (Register, CollapseAsPython)
{
    for (const auto& item : *fixture ("register.json")["collapse"].getArray())
        EXPECT_EQ (collapse (item[0].toString().toStdString()), item[1].toString().toStdString()) << dump (item);
}

// WhatATemplateMatches

TEST (Register, ALiteralMatchesItself)
{
    EXPECT_TRUE (matches ("/state/recall", "/state/recall"));
    EXPECT_FALSE (matches ("/state/recall", "/state/recalling"));
}

TEST (Register, APlaceholderMatchesOneSegment)
{
    EXPECT_TRUE (matches ("/channel/{ch}/gain", "/channel/0/gain"));
    EXPECT_FALSE (matches ("/channel/{ch}/gain", "/channel/0/1/gain"));
}

TEST (Register, ReapersWildcardMatchesOneSegment)
{
    EXPECT_TRUE (matches ("/track/@/fx/@/fxparam/@/value", "/track/0/fx/0/fxparam/0/value"));
    EXPECT_FALSE (matches ("/track/@/fx/@/fxparam/@/value", "/track/0/fx/0/value"));
}

TEST (Register, APlaceholderInsideASegmentMatchesTheRestOfIt)
{
    // /MultiEncoder/azimuth{ch} -- the number is glued to the word.
    EXPECT_TRUE (matches ("/MultiEncoder/azimuth{ch}", "/MultiEncoder/azimuth0"));
    EXPECT_FALSE (matches ("/MultiEncoder/azimuth{ch}", "/MultiEncoder/elevation0"));
}

TEST (Register, AnOscWildcardReachesAcrossSegments)
{
    // dispatcher.map("/channel/*") subscribes to the whole branch, so this has to match underneath it.
    EXPECT_TRUE (matches ("/channel/" + star, "/channel/0/gain"));
    EXPECT_TRUE (matches ("/channel/" + star, "/channel/0/eq/high"));
}

TEST (Register, AWildcardInTheMiddleMatches)
{
    const auto pattern = "/channel/" + star + "/led/" + star;
    EXPECT_TRUE (matches (pattern, "/channel/2/led/pfl"));
    EXPECT_FALSE (matches (pattern, "/channel/2/pfl"));
}

TEST (Register, ARegexCharacterInAnAddressIsACharacter)
{
    EXPECT_TRUE (matches ("/channel/{ch}/aux-send", "/channel/1/aux-send"));
    EXPECT_TRUE (matches ("/scroll/x/+", "/scroll/x/+"));
    EXPECT_FALSE (matches ("/scroll/x/+", "/scroll/x/"));
}

TEST (Register, TheMatcherAnswersAsPythonDoes)
{
    // Templates against addresses, 1000 answers of re.match, newlines and odd braces included.
    const auto& matrix = *fixture ("register.json")["matcher"].getArray();
    ASSERT_GT (matrix.size(), 500);
    for (const auto& item : matrix)
        EXPECT_EQ (matches (item[0].toString().toStdString(), item[1].toString().toStdString()), static_cast<bool> (item[2]))
            << dump (item);
}

// TheCatalogueAgainstTheTraffic

TEST (Register, AnAddressThatArrivedCarriesItsCount)
{
    const auto result = withCounts (theRegister(), countsFor ({ { "/channel/0/gain", 7 }, { "/channel/1/gain", 3 } }));
    const auto gain = entryEndingIn (result, "gain");
    EXPECT_EQ (static_cast<int> (gain["count"]), 10);
    EXPECT_TRUE (static_cast<bool> (gain["seen"]));
}

TEST (Register, AnAddressNobodySendsIsADeadWire)
{
    // /channel/n/4d is the real one: Core listens for it and neither controller has ever sent it.
    const auto result = withCounts (theRegister(), {});
    for (const auto& item : *result["entries"].getArray())
    {
        EXPECT_FALSE (static_cast<bool> (item["seen"]));
        EXPECT_EQ (static_cast<int> (item["count"]), 0);
    }
}

TEST (Register, TheEntriesComeThroughWhole)
{
    const auto result = withCounts (theRegister(), {});
    EXPECT_EQ (result["entries"][0]["source"].toString(), "a3-mixer.py:62");
    EXPECT_EQ (result["entries"][0]["device"].toString(), "mixer");
}

TEST (Register, TheHeadOfTheRegisterComesThrough)
{
    const auto result = withCounts (theRegister(), {});
    EXPECT_EQ (a3core::dumpIndented (result["devices"], -1), "[\"core\",\"mixer\"]");
}

TEST (Register, HowManyAreDeadIsCountedOnceRatherThanByThePage)
{
    const auto result = withCounts (theRegister(), countsFor ({ { "/channel/0/gain", 1 } }));
    EXPECT_EQ (static_cast<int> (result["seen"]), 1);
    EXPECT_EQ (static_cast<int> (result["unseen"]), 1);
}

TEST (Register, TheCountsAreAddedUpAsPythonDoes)
{
    const auto& recordedCounts = fixture ("register.json")["counts_of"];
    std::vector<std::pair<std::string, long long>> rows;
    for (const auto& row : *recordedCounts["rows"].getArray())
        rows.emplace_back (row["address"].toString().toStdString(), static_cast<juce::int64> (row["count"]));
    const auto counts = countsOf (rows);
    EXPECT_EQ (counts.size(), static_cast<size_t> (recordedCounts["counts"].getDynamicObject()->getProperties().size()));
    for (const auto& property : recordedCounts["counts"].getDynamicObject()->getProperties())
        EXPECT_EQ (counts.at (property.name.toString().toStdString()), static_cast<juce::int64> (property.value));
}

TEST (Register, WithCountsAsPython)
{
    const auto& recordedCounts = fixture ("register.json")["with_counts"];
    Counts counts;
    for (const auto& property : recordedCounts["counts"].getDynamicObject()->getProperties())
        counts[property.name.toString().toStdString()] = static_cast<juce::int64> (property.value);
    expectJsonEqual (recordedCounts["result"], withCounts (recordedCounts["register"], counts), "with_counts");
}

// ABrokenFileIsNotAReasonToFall

TEST (Register, AMissingFileIsAnEmptyRegister)
{
    EXPECT_EQ (load (juce::File ("/nowhere/osc-register.json"))["entries"].getArray()->size(), 0);
}

TEST (Register, AProblemIsSaidOutLoudRatherThanHidden)
{
    // An empty register and a missing one look the same to a reader otherwise.
    EXPECT_TRUE (load (juce::File ("/nowhere/osc-register.json"))["problem"].toString().isNotEmpty());
}

TEST (Register, HalfAFileIsAnEmptyRegister)
{
    const auto result = loadText ("{\"entries\": [{\"address\": \"/a\"");
    EXPECT_EQ (result["entries"].getArray()->size(), 0);
    EXPECT_TRUE (result["problem"].toString().isNotEmpty());
}

TEST (Register, AFileOfTheWrongShapeIsAnEmptyRegister)
{
    const auto result = loadText ("[1, 2, 3]");
    EXPECT_EQ (result["entries"].getArray()->size(), 0);
    EXPECT_TRUE (result["problem"].toString().isNotEmpty());
}

TEST (Register, AnEntryShortOfAFieldDoesNotTakeTheRestWithIt)
{
    const auto entries = loadText ("{\"entries\": [{\"address\": \"/a\"}, {\"address\": \"/b\", \"device\": \"mixer\","
                                   "\"direction\": \"in\", \"source\": \"x:1\", \"note\": \"\"}]}")["entries"];
    ASSERT_EQ (entries.getArray()->size(), 2);
    EXPECT_EQ (entries[0]["address"].toString(), "/a");
    EXPECT_EQ (entries[1]["address"].toString(), "/b");
    EXPECT_EQ (entries[0]["device"].toString(), "");
}

TEST (Register, TheShippedRegisterLoads)
{
    const auto payload = load (juce::File (A3_CORE_TEST_DATA "/parity/traffic/osc-register.json"));
    EXPECT_EQ (payload["problem"].toString(), "");
    // A floor against a truncated file, not a count: at least one row for every address of the truth.
    const auto& shipped = fixture ("register.json")["shipped"];
    EXPECT_GE (payload["entries"].getArray()->size(), static_cast<int> (shipped["truth_addresses"]));
    EXPECT_EQ (payload["entries"].getArray()->size(), static_cast<int> (shipped["entries"]));
    expectJsonEqual (shipped["first"], payload["entries"][0], "the first entry");
}

TEST (Register, EveryShapeOfFileLoadsAsInPython)
{
    for (const auto& property : fixture ("register.json")["load"].getDynamicObject()->getProperties())
    {
        const auto name = property.name.toString().toStdString();
        const auto& expected = property.value;
        const auto actual = expected["text"].isVoid() ? load (juce::File ("/nowhere/osc-register.json"))
                                                      : loadText (expected["text"].toString().toStdString());
        EXPECT_EQ (actual["problem"].toString().isNotEmpty(), static_cast<bool> (expected["problem"])) << name;
        expectJsonEqual (expected["devices"], actual["devices"], name + " devices");
        expectJsonEqual (expected["directions"], actual["directions"], name + " directions");
        expectJsonEqual (expected["entries"], actual["entries"], name + " entries");
    }
}

TEST (Register, TheRouteCountsWhatCoreCouldNotRouteAsArrived)
{
    // register_as_json: both tables are put in, and the history is asked for with a cutoff of now.
    recorded::TempDir temp;
    const auto file = temp.dir.getChildFile ("osc-register.json");
    recorded::writeBytes (file, "{\"entries\": [{\"address\": \"/channel/{ch}/gain\", \"device\": \"mixer\"},"
                                "{\"address\": \"/track/@/volume\", \"device\": \"reaper\"},"
                                "{\"address\": \"/never/sent\", \"device\": \"core\"}]}");
    Traffic traffic;
    seenFloat (traffic, Direction::in, "/channel/3/gain", 0.5, "mixer");
    unknownOf (traffic, "/track/12/volume", pyFloat (0.1));
    const auto result = registerAsJson (file, traffic, tick());
    EXPECT_EQ (static_cast<int> (result["seen"]), 2);
    EXPECT_EQ (static_cast<int> (result["unseen"]), 1);
    EXPECT_EQ (static_cast<int> (result["entries"][1]["count"]), 1);
    EXPECT_FALSE (static_cast<bool> (result["entries"][2]["seen"]));
}
