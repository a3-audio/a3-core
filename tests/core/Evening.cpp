// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_evening.py (B0 f3345dc7feeb): the evening on disk, so a
// power cut does not cost it, and what of it is played back. The source-text cases
// (TheEngineActuallyDoesIt) belong to the task that wires the program. The rig's real evenings are
// recorded by tools/parity/record_state.py; the decks block of the evening is Task 16's.
#include <gtest/gtest.h>

#include <map>

#include "B0Truth.h"
#include "Evening.h"
#include "StateRecorded.h"

using namespace a3core;
using Played = std::vector<std::pair<std::string, OscArg>>;

namespace
{
juce::var evening (const std::vector<std::pair<std::string, juce::var>>& values)
{
    auto* inner = new juce::DynamicObject();
    for (const auto& [address, value] : values)
        inner->setProperty (juce::String::fromUTF8 (address.c_str()), value);
    auto* outer = new juce::DynamicObject();
    outer->setProperty ("values", juce::var (inner));
    return juce::var (outer);
}

using Map = std::map<std::string, OscArg>;

// dict(replayable(...)): the last value of an address wins.
Map replay (const std::vector<std::pair<std::string, juce::var>>& values)
{
    Map played;
    for (const auto& [address, value] : replayable (b0::truth(), evening (values)))
        played.insert_or_assign (address, value);
    return played;
}
}

// WhatIsWrittenDown

TEST (Evening, ItIsWhatWasPassedOn)
{
    Relayed relayed;
    relayed.note ("/channel/0/gain", pyFloat (0.5));
    relayed.note ("/master/volume", pyFloat (0.8));
    const auto state = eveningState (relayed);
    EXPECT_EQ (static_cast<double> (state["values"]["/channel/0/gain"]), 0.5);
    EXPECT_EQ (static_cast<double> (state["values"]["/master/volume"]), 0.8);
}

TEST (Evening, AListPassedOnIsWrittenAsAList)
{
    // B1 (a3-core 7a7b2441f74e): the return's word is noted as a list and json writes it as one.
    Relayed relayed;
    relayed.note ("/aux-return/stem", std::vector<OscArg> { pyInt (1), pyInt (0), pyInt (2) });
    EXPECT_EQ (dumpIndented (eveningState (relayed), 2), "{\n  \"values\": {\n    \"/aux-return/stem\": [\n      1,\n      0,\n      2\n    ]\n  }\n}");
}

TEST (Evening, AnEmptyEveningIsAnEmptyFile)
{
    const auto state = eveningState (Relayed());
    ASSERT_NE (state["values"].getDynamicObject(), nullptr);
    EXPECT_EQ (state["values"].getDynamicObject()->getProperties().size(), 0);
    EXPECT_EQ (dumpIndented (state, 2), "{\n  \"values\": {}\n}");
}

// WhatIsPlayedBack

TEST (Evening, TheContinuousValuesComeBack)
{
    // The filter's own words too: a list of prefixes would have gone on replaying /fx and dropped /filter.
    const auto played = replay ({ { "/channel/1/gain", 0.5 }, { "/channel/3/eq/high", 0.25 }, { "/channel/2/volume", 0.7 },
                                  { "/channel/2/filter/frequency", 0.4 }, { "/master/volume", 0.8 },
                                  { "/master/aux-return", 0.1 }, { "/filter/frequency", 0.33 } });
    EXPECT_EQ (played.size(), 7u);
}

TEST (Evening, AnEveningFromBeforeAuxComesBackUnderTheNewNames)
{
    const auto played = replay ({ { "/channel/2/fx-send", 0.3 }, { "/master/fx-return", 0.1 } });
    EXPECT_EQ (played, (Map { { "/channel/2/aux-send", pyFloat (0.3) }, { "/master/aux-return", pyFloat (0.1) } }));
}

TEST (Evening, AnOldNameOutOfRangeIsDroppedNotRaised)
{
    EXPECT_TRUE (replay ({ { "/channel/99/fx-send", 0.3 } }).empty());
    EXPECT_TRUE (replay ({ { "/channel/99999999999999999999999/fx-send", 0.3 } }).empty());
}

TEST (Evening, LampsAreStatusAndNotSettings)
{
    EXPECT_TRUE (replay ({ { "/channel/1/cue/led", 1.0 }, { "/filter/led", "low_pass" } }).empty());
}

TEST (Evening, WhatCoresOwnStateFileAlreadyKeepsIsLeftOut)
{
    EXPECT_TRUE (replay ({ { "/channel/1/cue", 1.0 }, { "/channel/1/filter", 1.0 }, { "/channel/4/3d", 0.5 }, { "/filter/mode", 1 } }).empty());
}

TEST (Evening, APositionIsNeverPlayedBack)
{
    for (const char* address : { "/channel/1/azimuth", "/channel/1/elevation", "/stemdeck/pointer", "/motion/pointer" })
        EXPECT_TRUE (replay ({ { address, 90.0 } }).empty()) << address;
}

TEST (Evening, AMusicPreviewIsNeverPlayedBack)
{
    EXPECT_TRUE (replay ({ { "/stemdeck/ahead", "drop" } }).empty());
}

TEST (Evening, AnAddressCoreDoesNotSetIsLeftAlone)
{
    EXPECT_TRUE (replay ({ { "/vu/3", 0.2 }, { "/beat", 1 }, { "/state/recall", 1 } }).empty());
}

TEST (Evening, AnEveningInTheOldWordsIsNotPlayedBack)
{
    EXPECT_TRUE (replay ({ { "/channel/0/gain", 0.5 }, { "/fx/frequency", 0.33 } }).empty());
}

TEST (Evening, AFileFromAnotherVersionDoesNotRaise)
{
    juce::String error;
    for (const char* text : { "{}", "{\"values\": null}", "{\"values\": [1, 2]}", "{\"values\": 7}", "[]", "[1]", "\"text\"", "null", "7" })
        EXPECT_TRUE (replayable (b0::truth(), parseJson (text, error)).empty()) << text;
    EXPECT_TRUE (replayable (b0::truth(), juce::var()).empty());
}

// The rig's real files

TEST (Evening, RenamedAddressesAreReplayedUnderTheirNewName)
{
    const auto cases = recorded::load ("evening.json")["files"];
    const auto played = replayable (b0::truth(), recorded::loadedFrom (recorded::text (cases["evening-old"]["text"])));
    ASSERT_EQ (played.size(), 4u);
    EXPECT_EQ (played[0], (std::pair<std::string, OscArg> { "/channel/2/aux-send", pyFloat (0.3) }));
    EXPECT_EQ (played[1], (std::pair<std::string, OscArg> { "/master/aux-return", pyFloat (0.1) }));
    EXPECT_EQ (played[2], (std::pair<std::string, OscArg> { "/channel/1/aux-send", pyFloat (0.25) }));
    EXPECT_EQ (played[3], (std::pair<std::string, OscArg> { "/channel/1/aux-send", pyFloat (0.5) }));   // "/channel/01/" reads as 1
}

TEST (Evening, ReplayableAsPython)
{
    const auto cases = recorded::load ("evening.json")["files"];
    int compared = 0;
    for (const auto& [name, entry] : cases.getDynamicObject()->getProperties())
    {
        const auto played = replayable (b0::truth(), recorded::loadedFrom (recorded::text (entry["text"])));
        Played expected;
        if (! entry.hasProperty ("raises"))   // Python raises on a file that is no object; nothing is replayed here
            for (const auto& item : *entry["replay"].getArray())
            {
                const auto& parts = *item.getArray();
                if (parts[1].toString() == "list")   // several arguments in Python; no scalar to compare
                    continue;
                expected.emplace_back (parts[0].toString().toStdString(), recorded::expectedArg (parts[1].toString(), parts[2]));
            }
        ASSERT_EQ (played.size(), expected.size()) << name.toString();
        for (size_t i = 0; i < expected.size(); ++i)
            EXPECT_EQ (played[i], expected[i]) << name.toString() << " " << i << " " << expected[i].first;
        ++compared;
    }
    EXPECT_GE (compared, 8);
}

TEST (Evening, WhatIsWrittenIsWhatPythonWrote)
{
    const auto written = recorded::load ("evening.json")["written"];
    Relayed relayed;
    for (const auto& item : *written["notes"].getArray())
        relayed.note ((*item.getArray())[0].toString().toStdString(),
                      recorded::expectedArg ((*item.getArray())[1].toString(), (*item.getArray())[2]));
    EXPECT_EQ (dumpIndented (eveningState (relayed), 2) + "\n", recorded::text (written["text"]));
}
