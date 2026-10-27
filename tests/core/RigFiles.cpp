// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The rig's own state files (a3nuc1's ~/.local/state/a3-core, copied by hand) through start-up, the
// engine's hello and Core's token recall, against what B1's Python Core did with the same files.
// The files are private rig data and never in the repo: A3_RIG_STATE names the directory holding
// them, A3_RIG_EXPECTED the transcript tools/parity/record_rig.py recorded from them. Without
// A3_RIG_STATE the test is skipped.
#include <gtest/gtest.h>

#include "ScenarioRunner.h"

namespace
{
const std::string scenarioName = "startup-on-rig-files";

juce::String environment (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); }

juce::var parsedFile (const juce::File& file)
{
    auto value = juce::JSON::parse (file.loadFileAsString());
    if (value.isVoid())
        throw std::runtime_error (file.getFullPathName().toStdString() + " is not JSON");
    return value;
}

// The committed steps, with the rig's files written in verbatim (a string is written as it is).
juce::var scenarioOn (const juce::File& rigState)
{
    const auto steps = scenarios::rigDirectory().getChildFile (scenarioName + ".json");
    if (! steps.existsAsFile())
        throw std::runtime_error ("no rig scenario at " + steps.getFullPathName().toStdString());
    auto scenario = parsedFile (steps);
    auto* files = new juce::DynamicObject();
    for (const auto* name : { "state.json", "evening.json", "seen.json" })
        if (const auto file = rigState.getChildFile (name); file.existsAsFile())
            files->setProperty (name, file.loadFileAsString());
    scenario.getDynamicObject()->setProperty ("files", juce::var (files));
    return scenario;
}
}

TEST (RigFiles, RecallMatchesPython)
{
    const auto rigState = environment ("A3_RIG_STATE");
    if (rigState.isEmpty())
        GTEST_SKIP() << "A3_RIG_STATE not set";
    const auto expectedPath = environment ("A3_RIG_EXPECTED");
    ASSERT_TRUE (expectedPath.isNotEmpty())
        << "A3_RIG_EXPECTED not set: record B1's transcript of these files with tools/parity/record_rig.py";
    const juce::File expected (expectedPath);
    ASSERT_TRUE (expected.existsAsFile()) << expectedPath << " does not exist";

    const auto outcome = scenarios::run (scenarioName, scenarioOn (juce::File (rigState)), parsedFile (expected));
    std::string shown;
    for (const auto& difference : outcome.differences)
        shown += "  " + difference + "\n";
    EXPECT_TRUE (outcome.differences.empty())
        << "the rig's files recall differently from the Python Core:\n" << shown
        << "  the whole transcript: " << outcome.actualFile.getFullPathName().toStdString();
}
