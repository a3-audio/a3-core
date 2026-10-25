// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DjFilter.h"
#include <algorithm>
#include <cmath>
#include "Laws.h"

namespace a3
{
namespace
{
constexpr double weightSeconds = 0.010;     // R6
constexpr double cutoffSeconds = 0.020;
constexpr double svfBaseQ = 0.7071;
constexpr double svfQPerResonanceOctave = 20.0;    // Q = 0.7071 * 20^r
constexpr float ladderMaxResonance = 0.95f;
constexpr double maxCutoffOfRate = 0.45;     // keep the TPT tangent finite

float svfQ (float r) { return (float) (svfBaseQ * std::pow (svfQPerResonanceOctave, (double) r)); }
}

void DjFilter::prepare (double newRate, int newMaxBlock, int numChannels)
{
    sampleRate = newRate;
    maxBlock = std::max (1, newMaxBlock);
    jassert (numChannels >= 1 && numChannels <= maxChannels);
    channels = std::clamp (numChannels, 1, maxChannels);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, (juce::uint32) channels };
    svf.prepare (spec);
    ladder.prepare (spec);
    ladderOut.assign ((size_t) channels * (size_t) maxBlock, 0.0f);
    cutoffs.assign ((size_t) maxBlock, 0.0f);
    resonances.assign ((size_t) maxBlock, 0.0f);
    dips.assign ((size_t) maxBlock, 1.0f);
    for (auto* s : { &cutoff, &resonance })
        s->reset (sampleRate, cutoffSeconds);
    for (auto* s : { &svfWeight, &ladderWeight, &dipGain })
        s->reset (sampleRate, weightSeconds);
    reset();
}

void DjFilter::reset()
{
    svf.reset();
    ladder.reset();
    dipGain.setCurrentAndTargetValue (1.0f);
    svfWeight.setCurrentAndTargetValue (0.0f);
    ladderWeight.setCurrentAndTargetValue (0.0f);
    grid.reset();
    ladderPhase = 0;
    initialised = false;
    lastResonance = lastCutoff = -1.0f;
}

void DjFilter::latch (bool on, const FilterControls& controls)
{
    const bool hp = controls.mode.load (std::memory_order_relaxed) != 0;
    const double hz = std::min (laws::filterCutoffHz (hp, (double) controls.frequency.load (std::memory_order_relaxed)),
                                maxCutoffOfRate * sampleRate);
    const float r = (float) laws::filterResonance ((double) controls.resonance.load (std::memory_order_relaxed));
    const bool useLadder = controls.kind.load (std::memory_order_relaxed) == (int) FilterKind::ladder;

    requestedHighPass = hp;
    if (! initialised)
    {
        highPass = hp;
        applyMode();
        cutoff.setCurrentAndTargetValue ((float) hz);
        resonance.setCurrentAndTargetValue (r);
        ladder.setCutoffFrequencyHz ((float) hz);
        ladder.setResonance (ladderMaxResonance * r);
        ladder.reset();
        initialised = true;
    }
    dipGain.setTargetValue (requestedHighPass == highPass ? 1.0f : 0.0f);
    cutoff.setTargetValue ((float) hz);
    resonance.setTargetValue (r);
    svfWeight.setTargetValue (on && ! useLadder ? 1.0f : 0.0f);
    ladderWeight.setTargetValue (on && useLadder ? 1.0f : 0.0f);
}

void DjFilter::process (float* const* io, int numChannels, int numSamples, bool on, const FilterControls& controls)
{
    juce::ScopedNoDenormals noDenormals;
    if (maxBlock == 0 || numChannels != channels)
        return;   // not prepared for this width
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int chunk = std::min (maxBlock, numSamples - offset);
        float* chunkIo[maxChannels];
        for (int ch = 0; ch < channels; ++ch)
            chunkIo[ch] = io[ch] + offset;
        grid.advance (chunk,
                      [&] { latch (on, controls); },
                      [&] (int begin, int count)
                      {
                          float* part[maxChannels];
                          for (int ch = 0; ch < channels; ++ch)
                              part[ch] = chunkIo[ch] + begin;
                          processChunk (part, count);
                      });
    }
}

void DjFilter::applyMode()
{
    svf.setType (highPass ? juce::dsp::StateVariableTPTFilterType::highpass : juce::dsp::StateVariableTPTFilterType::lowpass);
    ladder.setMode (highPass ? juce::dsp::LadderFilterMode::HPF24 : juce::dsp::LadderFilterMode::LPF24);
}

// The dip has reached 0: change the type, clear the state, fade back in.
void DjFilter::switchMode()
{
    highPass = requestedHighPass;
    applyMode();
    svf.reset();
    ladder.reset();
    lastCutoff = lastResonance = -1.0f;
    dipGain.setTargetValue (1.0f);
}

// Splits the chunk where the dip bottoms out, so that the type switches between two samples.
void DjFilter::processChunk (float* const* io, int count)
{
    for (int done = 0; done < count;)
    {
        int length = 0;
        bool bottomedOut = false;
        while (done + length < count && ! bottomedOut)
        {
            cutoffs[(size_t) length] = cutoff.getNextValue();
            resonances[(size_t) length] = resonance.getNextValue();
            dips[(size_t) length] = dipGain.getNextValue();
            bottomedOut = requestedHighPass != highPass && dips[(size_t) length] == 0.0f;
            ++length;
        }
        float* part[maxChannels];
        for (int ch = 0; ch < channels; ++ch)
            part[ch] = io[ch] + done;
        render (part, length);
        if (bottomedOut)
            switchMode();
        done += length;
    }
}

void DjFilter::render (float* const* io, int count)
{
    // Ladder: sub-blocks counted across calls, so the result does not depend on the host's period.
    for (int done = 0; done < count;)
    {
        const int len = std::min (count - done, ladderSubBlock - ladderPhase);
        if (ladderPhase == 0)
        {
            ladder.setCutoffFrequencyHz (cutoffs[(size_t) done]);
            ladder.setResonance (ladderMaxResonance * resonances[(size_t) done]);
        }
        float* planes[maxChannels];
        for (int ch = 0; ch < channels; ++ch)
        {
            float* out = ladderOut.data() + (size_t) ch * (size_t) maxBlock + done;
            std::copy (io[ch] + done, io[ch] + done + len, out);
            planes[ch] = out;
        }
        juce::dsp::AudioBlock<float> block (planes, (size_t) channels, (size_t) len);
        ladder.process (juce::dsp::ProcessContextReplacing<float> (block));
        done += len;
        ladderPhase = (ladderPhase + len) % ladderSubBlock;
    }

    for (int i = 0; i < count; ++i)
    {
        if (cutoffs[(size_t) i] != lastCutoff)
            svf.setCutoffFrequency (lastCutoff = cutoffs[(size_t) i]);
        if (resonances[(size_t) i] != lastResonance)
            svf.setResonance (svfQ (lastResonance = resonances[(size_t) i]));
        const float dip = dips[(size_t) i];
        const float wSvf = svfWeight.getNextValue() * dip, wLadder = ladderWeight.getNextValue() * dip;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float x = io[ch][i];
            const float filteredSvf = svf.processSample (ch, x);
            const float filteredLadder = ladderOut[(size_t) ch * (size_t) maxBlock + (size_t) i];
            io[ch][i] = x + wSvf * (filteredSvf - x) + wLadder * (filteredLadder - x);
        }
    }
}
}
