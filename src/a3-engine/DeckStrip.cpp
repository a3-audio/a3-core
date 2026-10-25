// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeckStrip.h"
#include <algorithm>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Laws.h"

namespace a3
{
void DeckStrip::prepare (double newRate, int maxBlockSize)
{
    sampleRate = newRate;
    maxBlock = std::max (1, maxBlockSize);
    for (auto& plane : x)
        plane.assign ((size_t) maxBlock, 0.0f);
    for (auto* gain : { &trim, &volume })
        gain->prepare (sampleRate);
    for (auto* silence : { &gainSilence, &gainSilenceAfterFilter, &volumeSilence })
        silence->prepare (sampleRate);
    eq.prepare (sampleRate);
    pre.prepare (sampleRate);
    post.prepare (sampleRate);
    if (sharedFilter != nullptr)
        filter.prepare (sampleRate, maxBlock, channels);
    grid.reset();
}

void DeckStrip::reset()
{
    eq.prepare (sampleRate);   // clears the filter states and the grid; allocates nothing
    if (sharedFilter != nullptr)
        filter.reset();
    grid.reset();
}

void DeckStrip::latchControls (const StripControls& controls)
{
    const auto load = [] (const std::atomic<float>& a) { return (double) a.load (std::memory_order_relaxed); };
    trim.setParameters ((float) laws::slopeVolume (load (controls.gain)), 1.0f);
    eq.setParameters ((float) laws::slopeEq (load (controls.eqHigh)), (float) laws::slopeEq (load (controls.eqMid)),
                      (float) laws::slopeEq (load (controls.eqLow)));
    volume.setParameters ((float) laws::slopeVolume (load (controls.volume)), 1.0f);

    const bool gainOpen = ! laws::isSilent (load (controls.gain));
    gainSilence.setOpen (gainOpen);
    gainSilenceAfterFilter.setOpen (gainOpen);
    volumeSilence.setOpen (gainOpen && ! laws::isSilent (load (controls.volume)));
}

void DeckStrip::process (const float* const* in16, const DeckTaps& taps, int numSamples, const StripControls& controls)
{
    juce::ScopedNoDenormals noDenormals;
    if (maxBlock == 0)
        return;   // not prepared
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int chunk = std::min (maxBlock, numSamples - offset);
        grid.advance (chunk,
                      [&] { latchControls (controls); },
                      [&] (int begin, int count)
                      {
                          const int at = offset + begin;
                          const float* in[channels];
                          DeckTaps part;
                          for (int ch = 0; ch < channels; ++ch)
                          {
                              in[ch] = in16[ch] + at;
                              part.preVolume[ch] = taps.preVolume[ch] != nullptr ? taps.preVolume[ch] + at : nullptr;
                              part.postVolume[ch] = taps.postVolume[ch] != nullptr ? taps.postVolume[ch] + at : nullptr;
                          }
                          processStretch (in, part, count, controls);
                      });
    }
}

void DeckStrip::processStretch (const float* const* in, const DeckTaps& taps, int count, const StripControls& controls)
{
    float* xs[channels];
    for (int ch = 0; ch < channels; ++ch)
    {
        xs[ch] = x[(size_t) ch].data();
        std::copy (in[ch], in[ch] + count, xs[ch]);
    }
    const auto copyTo = [&] (float* const* taps16)
    {
        for (int ch = 0; ch < channels; ++ch)
            if (taps16[ch] != nullptr)
                std::copy (xs[ch], xs[ch] + count, taps16[ch]);
    };
    const auto pushW = [&] (Meters& meter)
    {
        const float* w[2] { xs[0], xs[0] };
        meter.push (w, count);
    };

    trim.process (xs, count, channels);
    eq.process (xs, count, channels);
    gainSilence.process (xs, count, channels);
    pushW (pre);

    if (sharedFilter != nullptr)
        filter.process (xs, channels, count, controls.filterOn.load (std::memory_order_relaxed) >= 0.5f, *sharedFilter);
    gainSilenceAfterFilter.process (xs, count, channels);
    copyTo (taps.preVolume);

    volume.process (xs, count, channels);
    volumeSilence.process (xs, count, channels);
    copyTo (taps.postVolume);
    pushW (post);
}

StripMeter DeckStrip::preMeterW() { return { pre.takePeak (0), pre.rms (0) }; }
StripMeter DeckStrip::postMeterW() { return { post.takePeak (0), post.rms (0) }; }
}
