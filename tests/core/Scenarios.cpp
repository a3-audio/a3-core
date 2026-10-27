// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The scenarios of tests/core/scenarios, recorded from B1's Python Core (tools/parity/record_scenario.py),
// replayed against the C++ Core. One TEST per scenario, named after it in CamelCase.
#include <gtest/gtest.h>

#include "ScenarioRunner.h"

namespace
{
void expectAsPython (const std::string& name)
{
    const auto outcome = scenarios::run (name);
    std::string shown;
    for (const auto& difference : outcome.differences)
        shown += "  " + difference + "\n";
    EXPECT_TRUE (outcome.differences.empty())
        << name << " differs from the Python Core:\n" << shown
        << "  the whole transcript: " << outcome.actualFile.getFullPathName().toStdString();
}
}

TEST (Scenarios, StartupQuiet) { expectAsPython ("startup-quiet"); }
