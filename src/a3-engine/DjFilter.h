// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "ControlBlock.h"

namespace a3
{
enum class FilterKind { svf = 0, ladder = 1 };

struct FilterControls   // desk/Motion values, as on the wire
{
    std::atomic<int> kind { 0 };        // FilterKind
    std::atomic<int> mode { 1 };        // 1 = high-pass, 0 = low-pass (Core's FX_MODE_NUMBERS)
    std::atomic<float> frequency { 0.5f }, resonance { 0.0f };
};

// The DJ filter on `numChannels` channels in place (2 for the analog strip, 16
// for the deck strip), one set of coefficients and weights for all of them.
//
// Both kinds always run. On/off and kind move through a 10 ms linear weight
// crossfade (dry, SVF, ladder); the cutoff is smoothed per sample over 20 ms
// toward the value latched on the 128-frame control grid. The ladder takes
// that cutoff every ladderSubBlock frames (its own smoothing covers the rest).
// A change of high/low-pass mode dips through dry: the filtered weight fades to
// 0 over 10 ms, the type switches there with the filter state reset, and the
// weight fades back over 10 ms. A mode request during the dip only retargets.
// At most maxChannels (64) channels.
class DjFilter
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels);   // allocates
    void reset();   // clears the filter state, no allocation; the controls are re-latched
    void process (float* const* io, int numChannels, int numSamples, bool on, const FilterControls& controls);

private:
    static constexpr int maxChannels = 64;
    static constexpr int ladderSubBlock = 32;

    void latch (bool on, const FilterControls& controls);
    void processChunk (float* const* io, int count);
    void render (float* const* io, int count);
    void switchMode();
    void applyMode();

    double sampleRate = 44100.0;
    int maxBlock = 0, channels = 0;
    bool initialised = false, highPass = true, requestedHighPass = true;
    float lastResonance = -1.0f, lastCutoff = -1.0f;
    ControlGrid grid;
    int ladderPhase = 0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> cutoff, resonance, svfWeight, ladderWeight, dipGain;
    juce::dsp::StateVariableTPTFilter<float> svf;
    juce::dsp::LadderFilter<float> ladder;
    std::vector<float> ladderOut, cutoffs, resonances, dips;   // channel-major scratch (ladderOut), per-sample curves
};
}
