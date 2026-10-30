// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's block and periods on PipeWire (AudioTiming.h): it sets the stored block at its start (the engine
// remembers it, nothing else sets it), applies what the AUDIO page chose, stores it in engine.json and keeps the
// previous choice for Revert. Control thread only.
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
    // After WirePlumber is back: the device's ports return and the engine links them (once a second), then the
    // round trip is measured again (the periods are in it).
    static constexpr double measureAfterSeconds = 3.0;

    struct Hooks
    {
        CommandRunner& runner;
        juce::File dropIn;                                          // periodsDropInFile
        std::function<juce::Result (const AudioTiming&)> store;     // into engine.json
        std::function<void()> measure;                              // the round trip, over the digital loop
    };
    explicit TimingControl (Hooks);

    // The stored block, forced at the engine's start. The periods are WirePlumber's to read at its own start.
    void start (const AudioTiming& stored);
    // A choice of the page: the block live, the periods through the drop-in and a WirePlumber restart (the page
    // asked first). `io`: the running I/O map, whose device gets the periods.
    juce::Result apply (const AudioTiming& wanted, const IoMap& io);
    void service (double nowSeconds);
    TimingReport report() const;

private:
    juce::Result applyBlock (int block);
    juce::Result applyPeriods (int periods, const IoMap& io);
    juce::Result fail (const juce::String& why);

    Hooks hooks;
    AudioTiming applied;
    std::optional<AudioTiming> previous;
    bool restarting = false;
    std::optional<double> measureAt;
    juce::String message;
};
}
