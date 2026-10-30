// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Start, stop and restart the rig's programs of this machine through `systemctl --user`, in the rig's order (the
// Canvas's StartPlan, a3-system canvas/Source/model): the engine and Core before the decks, the decks skipped when the
// engine did not come up. Pure plans and one executor; the commands run through a CommandRunner (posix_spawn).
#pragma once
#include "CommandRunner.h"
#include "RigModel.h"
#include <functional>

namespace a3app::rig
{
struct Step
{
    Command argv;
    juce::String what;      // what it starts or stops, as the log and PlanResult name it
    juce::String waitFor;   // a unit that must become active, or empty
    juce::String needs;     // a step's `what` that must not have failed, or empty
    bool mayFail = false;   // an exit code that is no news (stopping a desk that does not run)
};

// Each plan takes only the programs that run here (the caller passes them).
std::vector<Step> startAll (const std::vector<Program>& here);
std::vector<Step> stopAll (const std::vector<Program>& here);
std::vector<Step> start (const Program&);
std::vector<Step> stop (const Program&);
std::vector<Step> restart (const Program&);

// How long a waitFor is given: `attempts` looks, `pause` between them (the page: 30 x 500 ms).
struct Waiting
{
    int attempts = 30;
    std::function<void()> pause = [] { juce::Thread::sleep (500); };
};

struct PlanResult
{
    std::vector<juce::String> skipped, failed;
    bool ok() const { return failed.empty() && skipped.empty(); }
};

// `systemctl --user show` of one unit, now: its ActiveState.
using ActiveState = std::function<juce::String (const juce::String& unit)>;

// Runs the steps in order. A failure is recorded and the rest continue, except steps that need what failed: those are
// skipped (a failed engine skips the decks). `log` hears each command and each failure.
PlanResult execute (const std::vector<Step>&, CommandRunner&, const ActiveState&, const Waiting& = {},
                    const std::function<void (const juce::String&)>& log = {});
// "Start all: done", "Stop Deck A: 1 failed (a3-engine.service), 2 skipped".
juce::String outcome (const juce::String& title, const PlanResult&);
}
