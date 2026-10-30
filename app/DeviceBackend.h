// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AudioBackend.h"
#include "EnginePorts.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <juce_audio_devices/juce_audio_devices.h>

namespace a3app
{
// Per engine port (in enginePortNames' order): the device channel the I/O map gives it,
// or -1 when unmapped. A port without an entry is silent; it never takes a neighbour's place.
std::vector<int> deviceChannelFor (const IoMap&, const PortNames&, bool inputs);

// The audio thread's share, pure: points every engine port's slot at its device buffer, or at
// nullptr (silence / not written) when it is unmapped or its channel does not exist on the
// device. Device outputs are cleared first, so a channel no port writes is silent, not stale.
void bindDeviceBuffers (const std::vector<int>& inputChannels, const std::vector<int>& outputChannels,
                        const float* const* deviceIn, int numDeviceIn, float* const* deviceOut, int numDeviceOut,
                        int numSamples, PortSlots&);

// Why the device cannot carry the loop ("" when it can): the device opened `inputs` and `outputs` channels.
juce::String loopUnavailable (const std::optional<RoundTripLoop>&, int inputs, int outputs);

// A JUCE audio device (ALSA on Linux) for machines without the rig's JACK: the engine's ports
// are mapped to device channels by the I/O map (spec D9, "deviceChannels"). JUCE's own JACK
// device type is never compiled in.
class DeviceBackend final : public AudioBackend, private juce::AudioIODeviceCallback
{
public:
    DeviceBackend();
    ~DeviceBackend() override;

    juce::Result open (EngineHost&, const AppConfig&) override;
    void close() override;
    BackendStatus status() const override;
    std::vector<std::pair<std::string, std::string>> liveLinks() const override { return {}; }   // a device has no links
    // A device that restarted with another rate or block size rebuilds the host (no re-fade).
    void service (double nowSeconds) override;
    void measureRoundTrip() override;

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void audioDeviceError (const juce::String&) override;

    juce::AudioDeviceManager manager;
    EngineHost* host = nullptr;
    bool running = false;
    BackendSettings settings;
    double sampleRate = 0;

    std::vector<int> inputChannels, outputChannels;
    int loopInputChannel = -1, loopOutputChannel = -1;   // 0-based; -1: no loop, or the device lacks it
    std::unique_ptr<RoundTripMeter> roundTrip;
    a3::EngineInputs inputs;
    a3::EngineOutputs outputs;
    PortSlots slots;

    std::atomic<int> pendingBlockSize { 0 };
    std::atomic<int> pendingSampleRate { 0 };
    std::atomic<float> engineBlockMs { 0 };   // the engine's own time for the last block
    std::atomic<int> lastBlockFrames { 0 };
};
}
