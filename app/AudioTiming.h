// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The AUDIO page's block and periods on PipeWire (the JACK backend), pure: the commands the engine runs and the
// WirePlumber drop-in it writes. TimingControl.h runs them.
//
// Block: PipeWire's settings metadata, clock.force-quantum, live. In PipeWire 1.6 a forced quantum is not clamped
// by clock.min-quantum (settings.check-quantum is off by default; context.c takes min = max = force), so the
// shipped min-quantum 128 stays and a block of 32 or 64 still applies. The shipped block, 128, releases the force:
// PipeWire's own configuration (a3-core's pipewire.conf.d, quantum 128) runs exactly as before.
//
// Periods: api.alsa.period-num on the engine's own ALSA nodes, through a WirePlumber 0.5 drop-in; WirePlumber
// reads it when it starts, so a change restarts it (the device's nodes go and come back, about two seconds of
// silence). 3, PipeWire's own choice on the rig (buffer 384 = 3 x 128), needs no file.
#pragma once
#include "ConfigFile.h"
#include "IoMap.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <string>
#include <vector>

namespace a3app
{
constexpr int defaultBlock = 128, defaultPeriods = 3;
const std::vector<int>& pipewireBlocks();    // 32, 64, ... 1024 frames
const std::vector<int>& pipewirePeriods();   // 2, 3

struct AudioTiming
{
    int block = defaultBlock;
    int periods = defaultPeriods;
    bool operator== (const AudioTiming& other) const { return block == other.block && periods == other.periods; }
    bool operator!= (const AudioTiming& other) const { return ! (*this == other); }
};
AudioTiming timingOf (const BackendSettings&);

// What the AUDIO page shows of TimingControl (BackendStatus::timing). `available`: the engine runs on PipeWire (JACK).
struct TimingReport
{
    bool available = false;
    AudioTiming applied;
    std::optional<AudioTiming> previous;   // what Revert goes back to
    bool restarting = false;               // WirePlumber restarts for new periods
    juce::String message;                  // the last change's outcome
};

using Command = std::vector<std::string>;
Command forceQuantumCommand (int block);   // pw-metadata -n settings 0 clock.force-quantum <block, or 0 at 128>
Command defaultNodesCommand();             // pw-metadata -n default 0: lists, sets nothing
Command restartWirePlumberCommand();       // systemctl --user restart wireplumber

// The default sink and source as `pw-metadata -n default 0` lists them (default.audio.sink/source); empty when not.
struct DefaultNodes
{
    std::string sink, source;
};
DefaultNodes parseDefaultNodes (const std::string& pwMetadataOutput);

// The ALSA nodes the engine's I/O map reaches, sorted: "system:playback_N" / "system:capture_N" are the default sink
// and source (jack.default-as-system), and the digital and analog loops are on them too; a peer whose client is a
// node name ("alsa_output....:playback_AUX0") names it. Nothing else (StemDeck, AES67, beat-analyzer) is a device.
std::vector<std::string> alsaNodesOf (const IoMap&, const DefaultNodes&);

// ~/.config/wireplumber/wireplumber.conf.d/80-a3-engine-alsa.conf: the engine's own file, the only one it touches.
juce::File periodsDropInFile (const juce::File& home);
juce::String periodsDropInText (const std::vector<std::string>& nodes, int periods);
// Writes the drop-in (atomic replace), or removes it at the default periods. No node: refused (a rule without a
// match would be every ALSA device's).
juce::Result writePeriodsDropIn (const juce::File& file, const std::vector<std::string>& nodes, int periods);

// "Block 64 frames × 3 periods".
juce::String timingText (int block, int periods);
}
