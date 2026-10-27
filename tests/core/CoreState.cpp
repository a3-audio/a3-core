// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_state.py (B0 f3345dc7feeb), the part that is about what
// is kept: Core's own state, across a restart. Plus the rig's real files, recorded by
// tools/parity/record_state.py.
#include <gtest/gtest.h>

#include "StateRecorded.h"

using namespace a3core;
using recorded::aRig;

namespace
{
juce::var parsed (const char* text)
{
    juce::String error;
    auto value = parseJson (text, error);
    EXPECT_EQ (error, juce::String());
    return value;
}

bool has (const std::vector<std::string>& names, const std::string& name)
{
    return std::find (names.begin(), names.end(), name) != names.end();
}
}

// WhatComesBack

TEST (CoreState, ARoundTripReturnsEveryField)
{
    auto rig = aRig();
    rig.channels[0].toggleFx = true;
    rig.channels[2].toggleCue = true;
    rig.channels[3].toggleFx = true;
    rig.channels[1].threeD = 0.62;
    rig.master.fxMode = MasterInfo::FxMode::highPass;

    auto fresh = aRig();
    applyState (stateOf (rig.channels, rig.master), fresh.channels, fresh.master);

    for (size_t i = 0; i < rig.channels.size(); ++i)
    {
        EXPECT_EQ (fresh.channels[i].toggleFx, rig.channels[i].toggleFx) << i;
        EXPECT_EQ (fresh.channels[i].toggleCue, rig.channels[i].toggleCue) << i;
        EXPECT_EQ (fresh.channels[i].threeD, rig.channels[i].threeD) << i;
    }
    EXPECT_EQ (fresh.master.fxMode, MasterInfo::FxMode::highPass);
}

TEST (CoreState, TheFilterModeComesBackAsTheModeNotAsANumber)
{
    auto rig = aRig();
    rig.master.fxMode = MasterInfo::FxMode::highPass;
    juce::String error;
    const auto written = parseJson (juce::String (dumpIndented (stateOf (rig.channels, rig.master), 2)), error);

    auto fresh = aRig();
    applyState (written, fresh.channels, fresh.master);
    EXPECT_EQ (fresh.master.fxMode, MasterInfo::FxMode::highPass);
}

TEST (CoreState, ItSurvivesAJsonRoundTrip)
{
    auto rig = aRig();
    rig.channels[1].toggleFx = true;
    const auto text = dumpIndented (stateOf (rig.channels, rig.master), 2);
    juce::String error;
    const auto again = parseJson (juce::String (text), error);
    EXPECT_EQ (error, juce::String());
    EXPECT_EQ (dumpIndented (again, 2), text);
}

// WhatTheFileMayNotKnow

TEST (CoreState, AMissingFieldLeavesTheDefaultAlone)
{
    auto rig = aRig();
    rig.channels[0].toggleCue = true;
    applyState (parsed ("{\"channels\": [{\"toggle_fx\": true}]}"), rig.channels, rig.master);
    EXPECT_TRUE (rig.channels[0].toggleFx);
    EXPECT_TRUE (rig.channels[0].toggleCue);
}

TEST (CoreState, AFieldTheModuleDoesNotKnowIsIgnored)
{
    auto rig = aRig();
    applyState (parsed ("{\"channels\": [{\"toggle_fx\": true, \"nonsense\": 1, \"azimuth\": 9.0}]}"), rig.channels, rig.master);
    EXPECT_TRUE (rig.channels[0].toggleFx);
    EXPECT_FALSE (rig.channels[0].azimuth.has_value());
}

TEST (CoreState, AFileFromADeviceWithMoreChannelsIsTruncated)
{
    auto rig = aRig (2);
    applyState (parsed ("{\"channels\": [{\"toggle_fx\": true}, {\"toggle_fx\": true}, {\"toggle_fx\": true}, {\"toggle_fx\": true},"
                        "{\"toggle_fx\": true}, {\"toggle_fx\": true}, {\"toggle_fx\": true}, {\"toggle_fx\": true}]}"),
                rig.channels, rig.master);
    EXPECT_TRUE (rig.channels[0].toggleFx && rig.channels[1].toggleFx);
    EXPECT_EQ (rig.channels.size(), 2u);
}

TEST (CoreState, AFileFromADeviceWithFewerChannelsLeavesTheRest)
{
    auto rig = aRig (4);
    applyState (parsed ("{\"channels\": [{\"toggle_fx\": true}]}"), rig.channels, rig.master);
    EXPECT_TRUE (rig.channels[0].toggleFx);
    EXPECT_FALSE (rig.channels[3].toggleFx);
}

TEST (CoreState, AnEmptyStateChangesNothing)
{
    auto rig = aRig();
    applyState (parsed ("{}"), rig.channels, rig.master);
    EXPECT_EQ (dumpIndented (stateOf (rig.channels, rig.master), 2), dumpIndented (stateOf (aRig().channels, aRig().master), 2));
    EXPECT_EQ (rig.master.fxMode, MasterInfo::FxMode::lowPass);
}

TEST (CoreState, AFilterModeThatIsNotOneIsLeftAlone)
{
    auto rig = aRig();
    applyState (parsed ("{\"fx_mode\": 99}"), rig.channels, rig.master);
    EXPECT_EQ (rig.master.fxMode, MasterInfo::FxMode::lowPass);
}

// WhatIsKeptAndWhatIsNot

TEST (CoreState, ThePositionIsNotRememberedAcrossARestart)
{
    EXPECT_FALSE (has (channelFields, "azimuth"));
    EXPECT_FALSE (has (channelFields, "elevation"));
    auto rig = aRig();
    rig.channels[0].azimuth = 12.0;
    rig.channels[0].elevation = 3.0;
    EXPECT_EQ (dumpIndented (stateOf (rig.channels, rig.master), 2).find ("azimuth"), std::string::npos);
}

TEST (CoreState, TheCrossfadeIsRememberedAndThatIsNotAContradiction)
{
    EXPECT_TRUE (has (channelFields, "three_d"));
}

TEST (CoreState, TheDeadCacheIsNotRemembered)
{
    EXPECT_FALSE (has (channelFields, "width"));
}

// CueSinceTheRename

TEST (CoreState, AnOldPflFlagComesBackAsCue)
{
    auto rig = aRig();
    applyState (parsed ("{\"channels\": [{\"toggle_pfl\": true}]}"), rig.channels, rig.master);
    EXPECT_TRUE (rig.channels[0].toggleCue);
}

TEST (CoreState, TheKnobIsKept)
{
    auto rig = aRig();
    rig.master.phonesMix = 0.3;
    auto back = aRig();
    applyState (stateOf (rig.channels, rig.master), back.channels, back.master);
    EXPECT_EQ (back.master.phonesMix, 0.3);
}

TEST (CoreState, TheReturnCueIsKept)
{
    auto rig = aRig();
    rig.master.returnCue = true;
    juce::String error;
    const auto written = parseJson (juce::String (dumpIndented (stateOf (rig.channels, rig.master), 2)), error);
    auto back = aRig();
    applyState (written, back.channels, back.master);
    EXPECT_TRUE (back.master.returnCue);
}

TEST (CoreState, TheClearedMachinesAreNoLongerKept)
{
    EXPECT_FALSE (has (masterFields, "cue_cleared_on"));
    auto rig = aRig();
    EXPECT_EQ (dumpIndented (stateOf (rig.channels, rig.master), 2).find ("cue_cleared_on"), std::string::npos);
}

TEST (CoreState, AnOldFileWithClearedMachinesStillLoads)
{
    auto back = aRig();
    applyState (parsed ("{\"phones_mix\": 0.5, \"return_cue\": true, \"cue_cleared_on\": [\"local\", \"192.168.8.20\"]}"),
                back.channels, back.master);
    EXPECT_EQ (back.master.phonesMix, 0.5);
    EXPECT_TRUE (back.master.returnCue);
}

TEST (CoreState, AnOldFileHasTheReturnCueOff)
{
    auto back = aRig();
    applyState (parsed ("{\"phones_mix\": 0.5}"), back.channels, back.master);
    EXPECT_FALSE (back.master.returnCue);
}

TEST (CoreState, TheStemCueIsNoLongerWritten)
{
    auto rig = aRig();
    EXPECT_EQ (dumpIndented (stateOf (rig.channels, rig.master), 2).find ("stem_cue"), std::string::npos);
}

TEST (CoreState, AnOldStemCueIsIgnored)
{
    auto back = aRig();
    applyState (parsed ("{\"stem_cue\": true}"), back.channels, back.master);
    EXPECT_EQ (dumpIndented (stateOf (back.channels, back.master), 2), dumpIndented (stateOf (aRig().channels, aRig().master), 2));
}

// The stems are not this module's: the file carries them through.

TEST (CoreState, TheStemsAreWrittenLast)
{
    auto rig = aRig();
    const auto stems = parsed ("{\"cursors\": [1, 2]}");
    const auto written = dumpIndented (stateOf (rig.channels, rig.master, &stems), 2);
    EXPECT_NE (written.find ("\"stems\": {\n    \"cursors\""), std::string::npos);
    EXPECT_LT (written.find ("return_cue"), written.find ("stems"));
}

// The rig's real files

TEST (CoreState, OldFieldNamesAreRead)
{
    const auto cases = recorded::load ("state.json");
    const auto rig = recorded::rigFrom (recorded::text (cases["state-pfl"]["text"]));
    EXPECT_FALSE (rig.channels[0].toggleFx);
    EXPECT_TRUE (rig.channels[0].toggleCue);    // toggle_pfl: true
    EXPECT_TRUE (rig.channels[1].toggleFx);
    EXPECT_FALSE (rig.channels[1].toggleCue);   // toggle_cue says so; the old name does not override it
    EXPECT_TRUE (rig.channels[2].toggleCue);
    EXPECT_EQ (rig.channels[0].threeD, 0.25);
    EXPECT_EQ (rig.master.phonesMix, 0.5);
    EXPECT_FALSE (rig.master.returnCue);
}

TEST (CoreState, ReadsAsPythonReads)
{
    const auto cases = recorded::load ("state.json");
    int compared = 0;
    for (const auto& [name, entry] : cases.getDynamicObject()->getProperties())
    {
        const auto rig = recorded::rigFrom (recorded::text (entry["text"]));
        if (entry.hasProperty ("raises"))
        {
            // Python's apply_state raises on a file that is no object; a broken file is an empty state here.
            EXPECT_EQ (dumpIndented (stateOf (rig.channels, rig.master), 2), dumpIndented (stateOf (aRig().channels, aRig().master), 2))
                << name.toString();
            continue;
        }
        const auto& expected = entry["loaded"];
        for (size_t i = 0; i < rig.channels.size(); ++i)
        {
            const auto& channel = expected["channels"][static_cast<int> (i)];
            EXPECT_EQ (rig.channels[i].toggleFx, static_cast<bool> (channel["toggle_fx"])) << name.toString() << " " << i;
            EXPECT_EQ (rig.channels[i].toggleCue, static_cast<bool> (channel["toggle_cue"])) << name.toString() << " " << i;
            if (channel["three_d"].isVoid())
                EXPECT_FALSE (rig.channels[i].threeD.has_value()) << name.toString() << " " << i;
            else
                EXPECT_EQ (rig.channels[i].threeD, static_cast<double> (channel["three_d"])) << name.toString() << " " << i;
        }
        EXPECT_EQ (static_cast<int> (rig.master.fxMode), static_cast<int> (expected["master"]["fx_mode"])) << name.toString();
        EXPECT_EQ (rig.master.phonesMix, static_cast<double> (expected["master"]["phones_mix"])) << name.toString();
        EXPECT_EQ (rig.master.returnCue, static_cast<bool> (expected["master"]["return_cue"])) << name.toString();

        // stateOf of what was read: the layout the Python Core writes. state-numbers keeps its
        // ints in Python (true/1 stay what the file said); here they become what they mean.
        if (entry.hasProperty ("state_of") && name.toString() != "state-numbers")
        {
            juce::String error;
            const auto stems = parseJson (juce::String (recorded::text (entry["text"])), error)["stems"];
            EXPECT_EQ (dumpIndented (stateOf (rig.channels, rig.master, stems.isVoid() ? nullptr : &stems), 2) + "\n",
                       recorded::text (entry["state_of"])) << name.toString();
        }
        ++compared;
    }
    EXPECT_GE (compared, 7);
}

TEST (CoreState, ATextWhereANumberIsDueIsLeftAlone)
{
    // Python would store the text and fail later, at the first recall; a state file is a
    // convenience, so the value is skipped here.
    auto rig = aRig();
    rig.master.phonesMix = 0.4;
    applyState (parsed ("{\"phones_mix\": \"loud\", \"return_cue\": [], \"fx_mode\": \"1\","
                        "\"channels\": [{\"toggle_fx\": \"yes\", \"toggle_cue\": null, \"three_d\": \"half\"}, 7, null]}"),
                rig.channels, rig.master);
    EXPECT_EQ (rig.master.phonesMix, 0.4);
    EXPECT_FALSE (rig.master.returnCue);
    EXPECT_EQ (rig.master.fxMode, MasterInfo::FxMode::lowPass);
    EXPECT_FALSE (rig.channels[0].toggleFx);
    EXPECT_FALSE (rig.channels[0].threeD.has_value());
}

TEST (CoreState, ACrossfadeSetToNullIsForgotten)
{
    auto rig = aRig();
    rig.channels[0].threeD = 0.5;
    applyState (parsed ("{\"channels\": [{\"three_d\": null}]}"), rig.channels, rig.master);
    EXPECT_FALSE (rig.channels[0].threeD.has_value());   // Python sets None
}
