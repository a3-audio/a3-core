// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace a3
{
constexpr float noiseRmsDb = -20.0f;          // the pink noise's level, dBFS RMS
constexpr double noiseBurstSeconds = 1.0;     // one tap, one burst
constexpr double noiseFadeSeconds = 0.010;    // in and out, so no burst starts or stops with a click

// The speaker map's test noise for one zone: a burst of pink noise (Paul Kellet's refined filter over a
// seeded white source, so a run is repeatable) at noiseRmsDb, noiseBurstSeconds long, faded in and out
// over noiseFadeSeconds. It goes on one decoder output channel, or from a direction into the decoder's
// 16-channel input (order 3, the zone's normalisation). A new request while a burst plays restarts it at
// the new target: the old one fades out while the new one fades in.
//
// Threads: requests from any thread; they reach the audio thread through atomics only. The audio thread
// calls render, then addToInput and addToOutputs, once per chunk; none of them allocates.
class ZoneNoise
{
public:
    explicit ZoneNoise (bool inputIsSn3d);   // the zone decoder's normalisation (its useSN3D)

    void prepare (double sampleRate, int blockSize);   // allocates; calibrates the level; silent after
    void requestChannel (int channel);                 // 1-based decoder output channel; < 1 is ignored
    void requestDirection (float azimuthDegrees, float elevationDegrees);   // non-finite is ignored

    // Audio thread, count <= the prepared block size. bus16: the decoder's input (16 channels);
    // outputs: its output channels (a channel beyond them stays silent).
    void render (int count);
    void addToInput (float* const* bus16, int count) const;
    void addToOutputs (float* const* outputs, int numOutputs, int count) const;
    // Audio thread, for tests: the direction the last render took from a request, if it took one.
    std::optional<std::pair<float, float>> takenDirection() const { return taken; }

private:
    static constexpr int busChannels = 16;
    enum Kind { none = 0, channel = 1, direction = 2 };

    struct Voice
    {
        Kind kind = none;
        int channel = 0;
        std::array<float, busChannels> gains {};   // per input channel, a direction's
        int position = 0;                          // samples since it started
        bool releasing = false;
        float level = 0.0f, releaseStep = 0.0f;
    };

    struct Pink
    {
        uint32_t seed = 0x2545f491u;
        float b[7] {};
        float next();
    };

    static uint64_t pack (float azimuth, float elevation);
    static std::pair<float, float> unpack (uint64_t);
    void takeRequest();
    void start (Voice&, Kind, int channelNumber, float azimuth, float elevation) const;
    float envelope (Voice&);

    const bool sn3d;
    std::atomic<int> pendingKind { none };
    std::atomic<int> pendingChannel { 0 };
    std::atomic<uint64_t> pendingDirection { 0 };   // azimuth and elevation in one word: never one request's
                                                    // azimuth with another's elevation

    Pink pink;
    float scale = 1.0f;
    int burstLength = 1, fadeLength = 1;
    Voice playing, fading;   // the burst, and the one it replaced while that fades out
    std::array<Voice, 2> rendered;   // both as the last render began
    std::optional<std::pair<float, float>> taken;
    std::vector<float> noise;
    std::array<std::vector<float>, 2> curves;   // playing, fading: level x noise per sample
};
}
