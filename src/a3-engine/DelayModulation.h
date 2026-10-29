// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EngineConfig.h"
#include "IemNode.h"
#include "DelayParameters.h"
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

// BEAT and FEEDBK on the audio thread: DualDelay reads delayBPM, delayMult, feedback and
// xfeedback raw per block without a listener, so set() writes them there, once per block before
// the node runs. The time is delay::delayTimeFor (tempo clamped to 20..400 BPM), on both lines;
// the plug-in glides it (0.5 Hz one-pole). The feedback moves at most the whole 0..0.95 range in
// 30 ms toward delay::feedbackGains (linear gain), as DualDelay applies it per block unsmoothed.
// A non-finite value leaves its part as it is.
class DelayLoop
{
public:
    explicit DelayLoop (IemNode& dualDelay);   // control thread: looks the parameters up, starts from the node's feedback
    void prepare (double sampleRate);
    void set (float tempo, float beats, float feedbackShare, int numSamples) noexcept;

private:
    struct Side
    {
        std::atomic<float>* bpm;
        std::atomic<float>* mult;
        std::atomic<float>* feedback;
        std::atomic<float>* xfeedback;
    };
    Side sides[2];
    float share;   // the loop gain now, f + x
    double sampleRate = 48000.0;
};

// COLOR and SPREAD on the control thread: DualDelay applies cutoffs and yaw through parameter
// listeners, so tick() runs where DelayModulation runs (ruling R7 above). Each tick glides the
// cutoffs of both lines a third of an octave toward delay::cutoffsFor (color) and the yaw 10
// degrees toward +spread (left line) / -spread (right line).
class DelayShape
{
public:
    explicit DelayShape (IemNode& dualDelay);   // starts from the node's left cutoffs and yaw
    void tick (float color, float spread, IemNode& dualDelay);

private:
    delay::Cutoffs cutoffs;
    float spread;
};
}
