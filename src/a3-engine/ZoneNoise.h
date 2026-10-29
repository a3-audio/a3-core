// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <memory>

namespace a3
{
// The speaker map's test noise for one zone: IEM AllRADecoder's NoiseBurst (one decoder output
// channel) and AmbisonicNoiseBurst (a direction, into the decoder's 16-channel input), used as they
// are: 0.5 s, band-limited, -10 dBFS, faded. One burst of each kind at a time; a request while
// one runs is dropped (IEM's behaviour).
//
// Threads: the requests from any thread; they reach the audio thread through atomics only, and the
// bursts start at its next process call.
class ZoneNoise
{
public:
    explicit ZoneNoise (bool inputIsSn3d);   // the zone decoder's normalisation (its useSN3D)
    ~ZoneNoise();

    void prepare (double sampleRate, int blockSize);   // allocates
    void requestChannel (int channel);                 // 1-based decoder output channel
    void requestDirection (float azimuthDegrees, float elevationDegrees);

    // Audio thread. bus16: the decoder's input (16 channels, order 3); outputs: its output channels.
    void addToInput (float* const* bus16, int count);
    void addToOutputs (float* const* outputs, int numOutputs, int count);

private:
    struct Bursts;
    std::unique_ptr<Bursts> bursts;
    std::atomic<int> pendingChannel { 0 };
    std::atomic<float> pendingAzimuth { 0.0f }, pendingElevation { 0.0f };
    std::atomic<bool> pendingDirection { false };
};
}
