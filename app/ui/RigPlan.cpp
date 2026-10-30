// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "RigPlan.h"

#include <algorithm>

namespace a3app::rig
{
namespace
{
const juce::String engineUnit = "a3-engine.service";
const juce::String coreUnit = "a3-core.service";
const char* const deskBinary = "/usr/bin/a3-desk";

juce::String joined (const std::vector<juce::String>& units)
{
    juce::StringArray names;
    for (const auto& unit : units)
        names.add (unit);
    return names.joinIntoString (" ");
}

Step unitStep (const char* verb, const std::vector<juce::String>& units)
{
    Step step;
    step.argv = { "systemctl", "--user", verb };
    for (const auto& unit : units)
        step.argv.push_back (unit.toStdString());
    step.what = joined (units);
    return step;
}

// The units open their windows on the screen the page is on.
Step importEnvironment()
{
    Step step;
    step.argv = { "systemctl", "--user", "import-environment", "DISPLAY", "XAUTHORITY" };
    step.what = "the session's screen";
    return step;
}

// The desk has no unit of its own; it runs as a transient one.
Step runDesk (const Program& desk)
{
    Step step;
    step.argv = { "systemd-run", "--user", "--unit=" + desk.units.front().upToLastOccurrenceOf (".service", false, false).toStdString(),
                  "--collect", deskBinary };
    step.what = desk.units.front();
    return step;
}

Step stopDesk (const Program& desk)
{
    auto step = unitStep ("stop", desk.units);
    step.mayFail = true;
    return step;
}

std::vector<Step> startSteps (const Program& program)
{
    switch (program.kind)
    {
        case Kind::engine:
        {
            std::vector<Step> steps;
            for (const auto& unit : program.units)
            {
                auto step = unitStep ("start", { unit });
                step.waitFor = unit;   // what follows attaches to them
                steps.push_back (step);
            }
            return steps;
        }
        case Kind::deck:
        {
            auto step = unitStep ("start", program.units);
            step.needs = engineUnit;
            return { step };
        }
        case Kind::mixer: return { runDesk (program) };
        case Kind::motion:
        case Kind::analyzer: return { unitStep ("start", program.units) };
    }
    return {};
}

std::vector<Step> stopSteps (const Program& program)
{
    switch (program.kind)
    {
        case Kind::engine:
        {
            std::vector<Step> steps;
            for (auto unit = program.units.rbegin(); unit != program.units.rend(); ++unit)
                steps.push_back (unitStep ("stop", { *unit }));
            return steps;
        }
        case Kind::mixer: return { stopDesk (program) };
        case Kind::deck:
        case Kind::motion:
        case Kind::analyzer: return { unitStep ("stop", program.units) };
    }
    return {};
}

std::vector<const Program*> ofKind (const std::vector<Program>& list, Kind kind)
{
    std::vector<const Program*> found;
    for (const auto& program : list)
        if (program.kind == kind)
            found.push_back (&program);
    return found;
}

std::vector<juce::String> deckUnits (const std::vector<Program>& list)
{
    std::vector<juce::String> units;
    for (const auto* deck : ofKind (list, Kind::deck))
        units.insert (units.end(), deck->units.begin(), deck->units.end());
    return units;
}

void append (std::vector<Step>& to, const std::vector<Step>& more) { to.insert (to.end(), more.begin(), more.end()); }

void appendKind (std::vector<Step>& to, const std::vector<Program>& list, Kind kind, std::vector<Step> (*stepsOf) (const Program&))
{
    for (const auto* program : ofKind (list, kind))
        append (to, stepsOf (*program));
}

bool cameUp (const juce::String& unit, const ActiveState& activeState, const Waiting& waiting)
{
    for (int attempt = 0; attempt < waiting.attempts; ++attempt)
    {
        const auto state = activeState (unit);
        if (state == "active")
            return true;
        if (state == "failed")
            return false;
        if (waiting.pause)
            waiting.pause();
    }
    return activeState (unit) == "active";
}

juce::String commandLine (const Command& argv)
{
    juce::StringArray words;
    for (const auto& word : argv)
        words.add (word);
    return words.joinIntoString (" ");
}
}

std::vector<Step> startAll (const std::vector<Program>& here)
{
    std::vector<Step> steps { importEnvironment() };
    appendKind (steps, here, Kind::engine, startSteps);
    appendKind (steps, here, Kind::analyzer, startSteps);
    if (const auto decks = deckUnits (here); ! decks.empty())
    {
        auto step = unitStep ("start", decks);
        step.needs = engineUnit;
        steps.push_back (step);
    }
    appendKind (steps, here, Kind::mixer, startSteps);
    appendKind (steps, here, Kind::motion, startSteps);
    return steps;
}

std::vector<Step> stopAll (const std::vector<Program>& here)
{
    std::vector<Step> steps;
    appendKind (steps, here, Kind::motion, stopSteps);
    appendKind (steps, here, Kind::mixer, stopSteps);
    if (const auto decks = deckUnits (here); ! decks.empty())
        steps.push_back (unitStep ("stop", decks));
    appendKind (steps, here, Kind::analyzer, stopSteps);
    appendKind (steps, here, Kind::engine, stopSteps);
    return steps;
}

std::vector<Step> start (const Program& program)
{
    std::vector<Step> steps { importEnvironment() };
    append (steps, startSteps (program));
    return steps;
}

std::vector<Step> stop (const Program& program) { return stopSteps (program); }

std::vector<Step> restart (const Program& program)
{
    std::vector<Step> steps { importEnvironment() };
    if (program.kind == Kind::mixer)
    {
        steps.push_back (stopDesk (program));
        steps.push_back (runDesk (program));
        return steps;
    }
    steps.push_back (unitStep ("restart", program.units));
    return steps;
}

PlanResult execute (const std::vector<Step>& steps, CommandRunner& runner, const ActiveState& activeState, const Waiting& waiting,
                    const std::function<void (const juce::String&)>& log)
{
    const auto say = [&log] (const juce::String& line) {
        if (log)
            log (line);
    };
    PlanResult result;
    for (const auto& step : steps)
    {
        if (step.needs.isNotEmpty() && std::find (result.failed.begin(), result.failed.end(), step.needs) != result.failed.end())
        {
            result.skipped.push_back (step.what);
            say (step.what + ": skipped, waiting for " + step.needs);
            continue;
        }
        say ("$ " + commandLine (step.argv));
        const int exitCode = runner.run (step.argv).exitCode;
        juce::String why;
        if (exitCode != 0 && ! step.mayFail)
            why = "failed (exit code " + juce::String (exitCode) + ")";
        else if (exitCode == 0 && step.waitFor.isNotEmpty() && ! cameUp (step.waitFor, activeState, waiting))
            why = "did not come up";
        if (why.isEmpty())
            continue;
        result.failed.push_back (step.what);
        say (step.what + ": " + why);
    }
    return result;
}

juce::String outcome (const juce::String& title, const PlanResult& result)
{
    if (result.ok())
        return title + ": done";
    juce::String text = title + ": " + juce::String ((int) result.failed.size()) + " failed";
    if (! result.failed.empty())
        text << " (" << joined (result.failed) << ")";
    if (! result.skipped.empty())
        text << ", " << juce::String ((int) result.skipped.size()) << " skipped";
    return text;
}
}
