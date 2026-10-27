// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "VuTaps.h"
#include <cmath>

namespace a3
{
VuTap::VuTap (const VuSettings& settings) { configure (settings); }

void VuTap::configure (const VuSettings& settings)
{
    attack = settings.rmsAttack;
    release = settings.rmsRelease;
}

// beat-analyzer's VuMeter::processMono: one block, its peak and its RMS, in float as the analyzer.
void VuTap::processMono (const float* samples, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;
    float blockPeak = 0.0f, sumSquares = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float magnitude = std::fabs (samples[i]);
        if (magnitude > blockPeak)
            blockPeak = magnitude;
        sumSquares += samples[i] * samples[i];
    }

    // VuMeter::followRms
    const float blockRms = std::sqrt (sumSquares / (float) numSamples);
    if (attack >= 1.0f)
        followed = blockRms;
    else if (blockRms > followed)
        followed = attack * blockRms + (1.0f - attack) * followed;
    else
        followed = release * blockRms + (1.0f - release) * followed;
    rmsOut.store (followed, std::memory_order_relaxed);

    // VuMeter::notePeak: raise the maximum unless the reader took it meanwhile; then retry against
    // zero, so a take never brings back the peak it just took.
    float current = peakSinceTake.load (std::memory_order_relaxed);
    while (blockPeak > current && ! peakSinceTake.compare_exchange_weak (current, blockPeak, std::memory_order_acq_rel))
    {
    }
}

float VuTap::takePeak() noexcept { return peakSinceTake.exchange (0.0f, std::memory_order_acq_rel); }
float VuTap::rms() const noexcept { return rmsOut.load (std::memory_order_relaxed); }

int VuTaps::vuNumber (int slot) { return slot < mapChannels ? slot + 1 : stereoFirstNumber + (slot - mapChannels); }

namespace
{
bool isFree (const std::string& name) { return name.empty() || name.rfind ("free", 0) == 0; }
}

VuTaps::VuTaps (const std::vector<std::string>& vuMeterNames, const VuSettings& settings)
    : slotNames ((size_t) slots), taps (new VuTap[(size_t) slots]), present ((size_t) slots, false)
{
    for (int slot = 0; slot < slots; ++slot)
    {
        const auto index = (size_t) vuNumber (slot) - 1;
        if (index < vuMeterNames.size())
            slotNames[(size_t) slot] = vuMeterNames[index];
        present[(size_t) slot] = ! isFree (slotNames[(size_t) slot]);
        taps[(size_t) slot].configure (settings);
    }
}

VuTap* VuTaps::tapFor (const std::string& name)
{
    for (size_t slot = 0; slot < slotNames.size(); ++slot)
        if (present[slot] && slotNames[slot] == name)
            return &taps[slot];
    return nullptr;
}

const std::vector<std::string>& VuTaps::names() const { return slotNames; }

void VuTaps::take (std::vector<float>& peaks, std::vector<float>& rms)
{
    peaks.assign ((size_t) slots, 0.0f);
    rms.assign ((size_t) slots, 0.0f);
    for (size_t slot = 0; slot < (size_t) slots; ++slot)
    {
        if (! present[slot])
            continue;
        peaks[slot] = taps[slot].takePeak();
        rms[slot] = taps[slot].rms();
    }
}
}
