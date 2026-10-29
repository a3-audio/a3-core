// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EngineConfig.h"
#include "IemNode.h"
#include "a3-delay/DelayParameters.h"
#include <atomic>
#include <vector>

namespace a3
{
// Modulates DualDelay parameters with slow LFOs (engine.json's delayModulation; REAPER's pitch LFO
// was one, R5 removed it). The plug-in applies them through parameter listeners, so this runs on
// the engine role's control tick (100 Hz) and never on the audio thread (ruling R7): tick() takes
// no buffer. The engine leaves out every entry on a parameter it owns (ownsDelayParameter).
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

// R15: the engine owns the delay's mode, turn, filters, feedback and time, whatever a saved spatial
// directory or engine.json says: rotate mode with pitch and roll 0 (DelayShape forces them),
// yaw / cutoffs / feedback / time from the delay page. A modulation of one of these is not run.
bool ownsDelayParameter (const juce::String& id);
std::vector<ModulatedParameter> withoutOwnedParameters (std::vector<ModulatedParameter>);
// "" when nothing saved is overridden; else one line naming the saved fx values the engine replaces
// (a mode other than rotate, a pitch or roll other than 0) and the modulations it leaves out. Said
// once at start, by --check-config and on STATUS.
juce::String delayOverrideNote (const juce::var& fxParameters, const std::vector<ModulatedParameter>& modulation);

// BEAT and FEEDBK on the audio thread: DualDelay reads delayBPM, delayMult, feedback and
// xfeedback raw per block without a listener, so set() writes them there, once per block before
// the node runs. The time is delay::delayTimeFor (tempo clamped to 20..400 BPM), on both lines;
// how the patched plug-in moves to it is delay::crossfadeAboveRatio's comment. Each change of the
// feedback is a linear ramp of the loop gain over 30 ms (DualDelay applies it per block, so a
// block's step is a 30 ms ramp's share), split by delay::feedbackGains. A non-finite value leaves
// its part as it is.
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
    float share;                    // the loop gain now, f + x
    float rampFrom, rampTo;         // the running ramp of the share
    double rampDone = 0.0, rampLength = 1.0;   // samples
    double sampleRate = 48000.0;
};

// COLOR and SPREAD on the control thread: DualDelay applies cutoffs and yaw through parameter
// listeners, so tick() runs where DelayModulation runs (ruling R7 above). The constructor forces
// R15's rotate mode with pitch and roll 0 and writes both lines' cutoffs and yaw (+-) from the left
// line's; each tick then glides the cutoffs of both lines a third of an octave toward
// delay::cutoffsFor (color) and the yaw 10 degrees toward +spread (left line) / -spread (right line).
class DelayShape
{
public:
    explicit DelayShape (IemNode& dualDelay);
    void tick (float color, float spread, IemNode& dualDelay);

private:
    void write (IemNode& dualDelay, bool cutoffsToo);
    delay::Cutoffs cutoffs;
    float spread;
};
}
