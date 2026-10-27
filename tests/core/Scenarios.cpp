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
TEST (Scenarios, DeskTextValues) { expectAsPython ("desk-text-values"); }
TEST (Scenarios, MotionFloats) { expectAsPython ("motion-floats"); }
TEST (Scenarios, FlagsAndLamps) { expectAsPython ("flags-and-lamps"); }
TEST (Scenarios, FilterModeBothSpellings) { expectAsPython ("filter-mode-both-spellings"); }
TEST (Scenarios, EngineSourceAndOpenTypes) { expectAsPython ("engine-source-and-open-types"); }
TEST (Scenarios, DeckRing) { expectAsPython ("deck-ring"); }
TEST (Scenarios, ReturnRing) { expectAsPython ("return-ring"); }
TEST (Scenarios, HelloGate) { expectAsPython ("hello-gate"); }
TEST (Scenarios, DeskFirstOverReplay) { expectAsPython ("desk-first-over-replay"); }
TEST (Scenarios, ValuesBeforeHelloIgnored) { expectAsPython ("values-before-hello-ignored"); }
TEST (Scenarios, ProdjValuesWhileLiveOff) { expectAsPython ("prodj-values-while-live-off"); }
TEST (Scenarios, EngineHelloReplaysThenOpens) { expectAsPython ("engine-hello-replays-then-opens"); }
TEST (Scenarios, KnownBootIdAgain) { expectAsPython ("known-boot-id-again"); }
TEST (Scenarios, EngineRestart) { expectAsPython ("engine-restart"); }
TEST (Scenarios, CoreRestartUnderOpenEngine) { expectAsPython ("core-restart-under-open-engine"); }
TEST (Scenarios, DeviceRecallDoesNotOpen) { expectAsPython ("device-recall-does-not-open"); }
TEST (Scenarios, DeckHelloAnd3d) { expectAsPython ("deck-hello-and-3d"); }
TEST (Scenarios, DeckBOnItsOwnPort) { expectAsPython ("deck-b-on-its-own-port"); }
TEST (Scenarios, DeckGoesAbsent) { expectAsPython ("deck-goes-absent"); }
TEST (Scenarios, MotionMovesAndReturns) { expectAsPython ("motion-moves-and-returns"); }
TEST (Scenarios, SecondMotionIsARival) { expectAsPython ("second-motion-is-a-rival"); }
TEST (Scenarios, DeskJoinsAndLeaves) { expectAsPython ("desk-joins-and-leaves"); }
TEST (Scenarios, FifthDeskRefused) { expectAsPython ("fifth-desk-refused"); }
TEST (Scenarios, ProdjLiveAndPointer) { expectAsPython ("prodj-live-and-pointer"); }
TEST (Scenarios, BeatToRemoteMotionAndDesks) { expectAsPython ("beat-to-remote-motion-and-desks"); }
TEST (Scenarios, TempoToEngine) { expectAsPython ("tempo-to-engine"); }
TEST (Scenarios, StemdeckTwoDeckReports) { expectAsPython ("stemdeck-two-deck-reports"); }
TEST (Scenarios, StemdeckAhead) { expectAsPython ("stemdeck-ahead"); }
TEST (Scenarios, DeviceHelloDiffers) { expectAsPython ("device-hello-differs"); }
