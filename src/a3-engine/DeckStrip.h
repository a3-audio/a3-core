// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <vector>
#include "ChannelStrip.h"

namespace a3
{
struct DeckTaps    // caller-owned, numSamples each; nullptr = off
{
    float* preVolume[16];    // after TRIM, EQ and the DJ filter, before VOLUME
    float* postVolume[16];
};

struct StripMeter { float peak, rms; };

// The 16-channel B-format deck strip: TRIM -> EQ -> DJ filter -> VOLUME, the
// same units and laws as ChannelStrip's analog path (PurestGain, SmoothEQ3,
// DjFilter, SilenceFade), every control acting identically on all 16 channels
// (one gain, one set of EQ and filter coefficients), so a plane wave keeps its
// direction. No Isolator3, no 3D, no positions. The controls are a
// StripControls per channel, owned by the caller and read on the 128-frame grid.
//
// Silence (R18): GAIN 0 closes the strip after EQ (the pre meter and the filter
// see zero) and after the filter (the pre-volume taps), VOLUME 0 or GAIN 0
// closes the post tap, each by a 10 ms SilenceFade.
//
// Meters read W (ACN 0): the pre meter after TRIM and EQ (before the filter,
// like the analog strip), the post meter after VOLUME.
class DeckStrip
{
public:
    static constexpr int channels = 16;

    void setSharedFilter (const FilterControls* controls) { sharedFilter = controls; }   // before prepare
    void prepare (double sampleRate, int maxBlock);   // allocates; the only call that may
    void reset();                                      // clears EQ and filter state (before a fade-in)
    void process (const float* const* in16, const DeckTaps& taps, int numSamples, const StripControls& controls);
    StripMeter preMeterW();    // RMS now, peak since the last call (the call takes it)
    StripMeter postMeterW();

private:
    void latchControls (const StripControls& controls);
    void processStretch (const float* const* in, const DeckTaps& taps, int count, const StripControls& controls);

    PurestGain trim, volume;
    SilenceFade gainSilence, gainSilenceAfterFilter, volumeSilence;
    SmoothEQ3 eq;
    DjFilter filter;
    const FilterControls* sharedFilter = nullptr;
    Meters pre, post;
    ControlGrid grid;
    double sampleRate = 44100.0;
    int maxBlock = 0;
    std::array<std::vector<float>, channels> x;
};
}
