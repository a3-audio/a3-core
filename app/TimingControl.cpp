// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimingControl.h"
#include "SystemDevice.h"
#include <algorithm>

namespace a3app
{
namespace
{
juce::String commandText (const Command& command)
{
    juce::StringArray words;
    for (const auto& word : command)
        words.add (juce::String (word));
    return words.joinIntoString (" ");
}

bool offered (const std::vector<int>& choices, int value) { return std::find (choices.begin(), choices.end(), value) != choices.end(); }
}

TimingControl::TimingControl (Hooks h) : hooks (std::move (h)) {}

void TimingControl::start (const AudioTiming& stored)
{
    applied = stored;
    if (const auto result = applyBlock (stored.block); result.wasOk())
        message = {};
}

juce::Result TimingControl::fail (const juce::String& why)
{
    message = why;
    return juce::Result::fail (why);
}

juce::Result TimingControl::applyBlock (int block)
{
    const auto command = forceQuantumCommand (block);
    if (const auto ran = hooks.runner.run (command); ran.exitCode != 0)
        return fail ("block " + juce::String (block) + " not set: " + commandText (command) + " failed");
    return juce::Result::ok();
}

juce::Result TimingControl::applyPeriods (int periods, const IoMap& io)
{
    const auto listed = hooks.runner.run (defaultNodesCommand());
    if (listed.exitCode != 0)
        return fail ("periods not set: " + commandText (defaultNodesCommand()) + " failed");
    const auto nodes = alsaNodesOf (io, parseDefaultNodes (listed.output));
    if (const auto written = writePeriodsDropIn (hooks.dropIn, nodes, periods); written.failed())
        return fail ("periods not set: " + written.getErrorMessage());
    if (! hooks.runner.start (restartWirePlumberCommand()))
        return fail ("periods written to " + hooks.dropIn.getFullPathName() + ", but " + commandText (restartWirePlumberCommand())
                     + " did not start: they apply at the next WirePlumber start");
    restarting = true;
    return juce::Result::ok();
}

juce::Result TimingControl::apply (const AudioTiming& wanted, const IoMap& io)
{
    if (restarting)
        return fail ("WirePlumber is restarting; choose again in a moment");
    if (! offered (pipewireBlocks(), wanted.block) || ! offered (pipewirePeriods(), wanted.periods))
        return fail ("not a choice: " + timingText (wanted.block, wanted.periods));
    if (wanted == applied)
        return juce::Result::ok();

    const auto before = applied;
    auto result = juce::Result::ok();
    if (wanted.block != applied.block)
    {
        result = applyBlock (wanted.block);
        if (result.wasOk())
            applied.block = wanted.block;
    }
    if (result.wasOk() && wanted.periods != applied.periods)
    {
        result = applyPeriods (wanted.periods, io);
        if (result.wasOk())
            applied.periods = wanted.periods;
    }
    if (applied == before)
        return result;

    previous = before;
    const auto stored = hooks.store (applied);
    if (result.wasOk())
        message = stored.wasOk() ? "set: " + timingText (applied.block, applied.periods)
                                 : "set, but not kept: " + stored.getErrorMessage();
    return result;
}

void TimingControl::service (double now)
{
    if (restarting)
        if (const auto exit = hooks.runner.finished())
        {
            restarting = false;
            if (*exit == 0)
                measureAt = now + measureAfterSeconds;
            else
                message = commandText (restartWirePlumberCommand()) + " failed (exit " + juce::String (*exit) + ")";
        }
    if (measureAt && now >= *measureAt)
    {
        measureAt.reset();
        hooks.measure();
    }
}

TimingReport TimingControl::report() const
{
    TimingReport report;
    report.available = true;
    report.applied = applied;
    report.previous = previous;
    report.restarting = restarting;
    report.message = message;
    return report;
}
}
