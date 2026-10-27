// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <optional>

namespace a3
{
enum class GatedZone { main = 0, booth = 1, phones = 2 };
constexpr int numGatedZones = 3;
struct GateLevels { float gain[numGatedZones] {}; };   // current linear gain per gated zone

// A zone's calibration trim and its silent-start gate in one gain (spec Scope 7,
// Settled 6, ruling R6): closed it is exactly 0; open it moves linearly in gain to
// the trim over 1 s; closing reaches 0 within 10 ms. A zone without a trim stays
// closed whatever is asked, it never falls back to 0 dB. prepare() closes it.
class OutputGate
{
public:
    void prepare (double sampleRate);                     // closes the gate
    void setTrim (std::optional<float> trimDb);           // nullopt -> closed forever
    void process (float* const* channels, int numChannels, int numSamples, bool open);
    float currentGain() const;                            // any thread
    void startAt (float gain);                            // hand-over on a swap; clamped to 0..trim

private:
    double trimGain() const;

    std::optional<float> trim;
    double openStep = 0.0, closeStep = 0.0, level = 0.0;
    double sampleRate = 44100.0;
    std::atomic<float> published { 0.0f };
};
}
