// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EngineConfig.h"
#include "IemNode.h"
#include <atomic>
#include <vector>

namespace a3
{
// REAPER modulates DualDelay's pitchL/pitchR with a slow LFO. The plug-in applies
// pitch through a parameter listener, so this runs on the host's control thread
// (about 50 Hz) and never on the audio thread (ruling R7): tick() takes no buffer.
//   value = base + depth * shape (t),  shape in [0, 1]
//   sine:     0.5 + 0.5 sin (2 pi rateHz t)
//   triangle: 0 -> 1 -> 0 once per period
//   random:   a new target in [0, 1] every 1 / rateHz seconds, linear between them,
//             from a fixed seed, so a run can be reproduced
// rateHz 0 (or an unknown shape) holds the base.
class DelayModulation
{
public:
    explicit DelayModulation (std::vector<ModulatedParameter>);
    void tick (double seconds, IemNode& dualDelay);

private:
    std::vector<ModulatedParameter> parameters;
};

// The tempo is written into DualDelay's raw BPM values, which the plug-in reads
// per block without a listener, so set() is safe from the audio thread.
// Clamped to 20..400 BPM, then held to the plug-in's own range; non-finite is ignored.
class DelayTempo
{
public:
    explicit DelayTempo (IemNode& dualDelay);   // control thread: looks the parameters up
    void set (float bpm) noexcept;

private:
    std::atomic<float>* bpmL;
    std::atomic<float>* bpmR;
    float lowest, highest;
};
}
