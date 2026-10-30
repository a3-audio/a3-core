// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The AUDIO page's block on PipeWire (the JACK backend), pure: the command the engine runs. TimingControl.h runs it.
//
// Block: PipeWire's settings metadata, clock.force-quantum, live. In PipeWire 1.6 a forced quantum is not clamped
// by clock.min-quantum (settings.check-quantum is off by default; context.c takes min = max = force), so the
// shipped min-quantum 128 stays and a block of 32 or 64 still applies. The shipped block, 128, releases the force:
// PipeWire's own configuration (a3-core's pipewire.conf.d, quantum 128) runs exactly as before.
//
// There are no periods (2026-10-30): PipeWire drives the Scarlett by timer, 2 periods measured 348 samples and 3
// periods 344. The WirePlumber drop-in an older engine wrote for them is removed at the start.
#pragma once
#include "ConfigFile.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <string>
#include <vector>

namespace a3app
{
constexpr int defaultBlock = 128;
const std::vector<int>& pipewireBlocks();    // 32, 64, ... 1024 frames

struct AudioTiming
{
    int block = defaultBlock;
    bool operator== (const AudioTiming& other) const { return block == other.block; }
    bool operator!= (const AudioTiming& other) const { return ! (*this == other); }
};
AudioTiming timingOf (const BackendSettings&);

// What the AUDIO page shows of TimingControl (BackendStatus::timing). `available`: the engine runs on PipeWire (JACK).
struct TimingReport
{
    bool available = false;
    AudioTiming applied;
    std::optional<AudioTiming> previous;   // what Revert goes back to
    juce::String message;                  // the last change's outcome
};

using Command = std::vector<std::string>;
Command forceQuantumCommand (int block);   // pw-metadata -n settings 0 clock.force-quantum <block, or 0 at 128>

// ~/.config/wireplumber/wireplumber.conf.d/80-a3-engine-alsa.conf, the periods drop-in an older engine wrote.
juce::File retiredPeriodsDropIn (const juce::File& home);
// Removes that file (only it); true when there was one. WirePlumber reads it only at its own start.
bool removeRetiredPeriodsDropIn (const juce::File& home);

// "Block 64 frames".
juce::String timingText (int block);
}
