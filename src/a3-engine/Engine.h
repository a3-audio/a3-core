// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <memory>
#include <vector>
#include "EngineConfig.h"
#include "EngineControls.h"
#include "IemNode.h"
#include "OutputGate.h"
#include "SpatialBus.h"
#include "SpatialConfig.h"

namespace a3
{
struct DeckInputs   // one StemDeck slot; nullptr = silence
{
    std::array<const float*, 16> bformat {}, aux {};
    std::array<const float*, 9> cue {};
};

struct EngineInputs
{
    std::vector<const float*> analog;   // analog input i (0-based); missing or nullptr = silence
    std::vector<DeckInputs> decks;      // config.decks slots; a missing slot is silence
};

// nullptr (or a missing vector entry) = not written.
struct EngineOutputs
{
    ZoneOutputs main, booth;
    float* rec[2] {};
    float* phones[2] {};
    std::vector<std::array<float*, 2>> pre, post;   // per channel: pre = after TRIM/EQ, post = moving + steady after VOLUME
};

// The whole A3 graph (spec Part A, Scope 1-6; plan 4 rulings R1, R6, R11, R13, R14, R18):
//
//   channel c   ChannelSelect (source): analog pair -> ChannelStrip, or deck slot k -> DeckStrip,
//               each path times its select ramp (both run during a crossfade)
//   enc_main    analog: AmbiEncoderBus (moving/steady post-VOLUME, return post-RETURN in the bed)
//               + deck: its 16 post-VOLUME channels added directly
//   enc_fx      the same sources x auxSendGain (no return) -> DualDelay (tempo from delayBpm)
//   enc_phones  pre-VOLUME taps x cueWeight [cue on] + post-VOLUME x mixWeight,
//               + return x (mixWeight + cueWeight [returnCue on])
//   main/booth  (enc_fx + enc_main) -> MASTER/BOOTH -> SimpleDecoder (fixedGainDb is not applied, R1)
//   rec         BinauralDecoder (enc_fx + enc_main)
//   phones      BinauralDecoder (enc_fx + enc_phones) + every deck's cue x cueWeight (mix) -> PHONES
//
// A deck channel ignores its positions, 3D, FREQ and Q. Every gain control at 0 is silence (R18);
// aux/cue/mix weights move over 10 ms (R6). The DualDelay pitch modulation runs only in
// serviceControls (R7).
//
// Output stage (Scope 7, Settled 6): main, booth and phones each pass an OutputGate
// (trim and gate in one gain) behind MASTER/BOOTH/PHONES; rec gets trims.rec and is never
// gated. A fresh or re-prepared Engine has every gate closed until gateOpen.
class Engine
{
public:
    static std::unique_ptr<Engine> create (const EngineConfig&, const SpatialConfig&, juce::String& error);
    ~Engine();

    void prepare (double sampleRate, int blockSize);   // allocates; warm-up; off the audio thread
    int preparedBlockSize() const;
    // Any numSamples >= 0; calls longer than the prepared block are processed in prepared-size chunks.
    void process (const EngineInputs&, const EngineOutputs&, int numSamples, EngineControls&);
    void serviceControls (double seconds);   // control thread: the delay's pitch modulation

    // The gates' current gains, and the hand-over to a new Engine on a swap: call
    // startGateAt after prepare and before the first process.
    GateLevels gateLevels() const;
    void startGateAt (const GateLevels&);
    // Any thread: closes main, booth and phones (10 ms) whatever gateOpen says, until released.
    // The host's fade-out before it swaps in a rebuilt Engine.
    void holdGateClosed (bool held);

    // One meter per output port, in port order: main (sub when the layout has one, then
    // tops 1..N), booth (the same), rec L/R, phones L/R, then per channel pre L/R, post L/R.
    // Peaks since the last call (the call takes them; one reader), RMS now; lock-free.
    struct OutputMeter { float peak, rms; };
    std::vector<OutputMeter> outputMeters();

    // Observation (tests, diagnostics); any thread.
    int analogSourceCount() const;
    float lastFxSendPeak() const;          // largest |sample| sent into the delay during the last process call
    float lastAnalogEncoderPeak() const;   // largest |sample| the analog encoder wrote to enc_main during the last call
    IemNode& delay();

private:
    struct Impl;
    explicit Engine (std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};
}
