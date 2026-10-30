// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's block on PipeWire (AudioTiming.h): it sets the stored block at its start (the engine remembers it,
// nothing else sets it), applies what the AUDIO page chose, stores it in engine.json and keeps the previous choice
// for Revert. Control thread only.
#pragma once
#include "AudioTiming.h"
#include "CommandRunner.h"
#include <functional>
#include <optional>

namespace a3app
{
class TimingControl
{
public:
    struct Hooks
    {
        CommandRunner& runner;
        std::function<juce::Result (const AudioTiming&)> store;     // into engine.json
    };
    explicit TimingControl (Hooks);

    // The stored block, forced at the engine's start.
    void start (const AudioTiming& stored);
    // A choice of the page, live. The backend measures the round trip again on the new quantum.
    juce::Result apply (const AudioTiming& wanted);
    TimingReport report() const;

private:
    juce::Result applyBlock (int block);
    juce::Result fail (const juce::String& why);

    Hooks hooks;
    AudioTiming applied;
    std::optional<AudioTiming> previous;
    juce::String message;
};
}
