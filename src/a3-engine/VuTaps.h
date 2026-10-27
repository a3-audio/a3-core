// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// The /vu meters beat-analyzer measured, measured by the engine (O13, P11): one VuTap per meter point,
// beat-analyzer's VuMeter (beat-analyzer 7dd100b, src/analysis/vu_meter.cpp) with the rig's values.
namespace a3
{
// The rig's beat-analyzer env (VU_RMS_ATTACK, VU_RMS_RELEASE, OSC_SEND_RATE; read 2026-10-26).
struct VuSettings
{
    float rmsAttack = 0.8f, rmsRelease = 0.2f;
    int rateHz = 25;
};

// One meter point; the audio thread writes, the control thread takes. Per call (the analyzer's JACK
// period): peak = max |x| of the block, kept as the maximum until taken (raw: no hold, no fall, the
// displays apply those); rms = the block's RMS, followed with attack towards a louder block and
// release towards a quieter one.
class VuTap
{
public:
    VuTap() = default;
    explicit VuTap (const VuSettings&);
    void configure (const VuSettings&);   // before the first processMono

    void processMono (const float* samples, int numSamples) noexcept;   // audio thread
    float takePeak() noexcept;            // the maximum since the last take; resets it (one reader)
    float rms() const noexcept;           // any thread

private:
    float attack = 0.8f, release = 0.2f;
    float followed = 0.0f;                // the audio thread's own
    std::atomic<float> peakSinceTake { 0.0f }, rmsOut { 0.0f };
};

// The /vu numbers 1-40 and 51-66 (the analyzer's 56 inputs) by the truth's vu_meters names: slot i is
// meter number vuNumber (i), named by the truth's entry vuNumber (i) - 1. A free name (free39, free67...)
// has no tap, and 41-50 (StemDeck's stem_*) are not slots: those numbers stay at 0.
class VuTaps
{
public:
    static constexpr int mapChannels = 40;          // /vu/1-40
    static constexpr int mapBlock = 10;             // sent in bundles of 10 (the analyzer's kVuMapBlock)
    static constexpr int stereoChannels = 16;       // /vu/51-66
    static constexpr int stereoBlock = 8;           // sent in bundles of 8 (kVuStereoBlock)
    static constexpr int stereoFirstNumber = 51;
    static constexpr int slots = mapChannels + stereoChannels;   // 56, the rig's NUM_VU_CHANNELS

    static int vuNumber (int slot);   // slot (0-based) -> n of /vu/{n}

    VuTaps (const std::vector<std::string>& vuMeterNames, const VuSettings&);   // allocates

    VuTap* tapFor (const std::string& name);              // nullptr for a free, stem_* or unknown name
    const std::vector<std::string>& names() const;         // per slot, "" where the truth names none
    void take (std::vector<float>& peaks, std::vector<float>& rms);   // control thread; 56 entries, /vu order

private:
    std::vector<std::string> slotNames;
    std::unique_ptr<VuTap[]> taps;
    std::vector<bool> present;
};
}
