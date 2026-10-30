// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimingControl.h"
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

juce::Result TimingControl::apply (const AudioTiming& wanted)
{
    if (! offered (pipewireBlocks(), wanted.block))
        return fail ("not a choice: " + timingText (wanted.block));
    if (wanted == applied)
        return juce::Result::ok();
    if (const auto result = applyBlock (wanted.block); result.failed())
        return result;

    previous = applied;
    applied = wanted;
    const auto stored = hooks.store (applied);
    message = stored.wasOk() ? "set: " + timingText (applied.block) : "set, but not kept: " + stored.getErrorMessage();
    return juce::Result::ok();
}

TimingReport TimingControl::report() const
{
    TimingReport report;
    report.available = true;
    report.applied = applied;
    report.previous = previous;
    report.message = message;
    return report;
}
}
