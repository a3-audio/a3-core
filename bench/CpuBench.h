// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
#include "Engine.h"
#include "Generator.h"
#include "SpatialConfig.h"
#include "VuTaps.h"

namespace a3bench
{
// What a3-engine-cpu-bench runs: the whole Engine, offline, every input fed.
struct CpuBenchOptions
{
    int decks = 0;               // deck slots; 0 = the truth's deck range (what the engine uses without a saved count)
    int channels = 4;            // the desk's physical channels
    double rate = 48000.0;
    int frames = 128;            // the block, as the rig's quantum
    int blocks = 20000;
    bool paced = false;          // sleep to the block period, so caches and clocks idle as on the rig
    std::string truth = "/usr/share/a3/a3-osc.json";
    std::string spatial;         // empty = chooseSpatialDirectory's default
};

// nullopt for an unknown option, a missing or non-numeric value, or a count below 1.
std::optional<CpuBenchOptions> parseCpuBenchArgs (const std::vector<std::string>& arguments);

// "stemdeck.deck-hello"'s deck range's upper bound; 0 if the text has none.
int deckCountOfTruth (const juce::String& truthJson);

double blockSharePercent (double blockMs, int frames, double rate);

// The worst case of the revision: every deck slot has signal on its B-format, AUX and cue inputs
// and every deck sits on a channel (as far as the channels reach); every channel has its AUX send,
// cue and filter open, PHONES MIX half open, the headphone model on.
class CpuBenchRig
{
public:
    static std::unique_ptr<CpuBenchRig> create (const CpuBenchOptions&, const a3::SpatialConfig&,
                                                const std::vector<std::string>& vuMeterNames, juce::String& error);
    ~CpuBenchRig();

    void process (int block);                         // one block of options.frames, from the looped signal
    void processWith (const a3::EngineInputs&);       // the same, with inputs the caller changed
    const a3::EngineInputs& inputsAt (int block) const;
    a3::EngineControls& controls();
    float mainEnergy() const;                         // sum of squares on the main zone in the last block
    float mainPeak() const;                           // largest |sample| on the main zone in the last block

private:
    struct Impl;
    explicit CpuBenchRig (std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};
}
