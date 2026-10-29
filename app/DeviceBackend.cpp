// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeviceBackend.h"
#include <algorithm>
#include <cstdio>
#include <set>

namespace a3app
{
std::vector<int> deviceChannelFor (const IoMap& io, const PortNames& ports, bool inputs)
{
    const auto& names = inputs ? ports.inputs : ports.outputs;
    std::vector<int> channels;
    channels.reserve (names.size());
    for (const auto& name : names)
    {
        const auto found = io.deviceChannels.find (name);
        channels.push_back (found == io.deviceChannels.end() ? -1 : found->second);
    }
    return channels;
}

void bindDeviceBuffers (const std::vector<int>& inputChannels, const std::vector<int>& outputChannels,
                        const float* const* deviceIn, int numDeviceIn, float* const* deviceOut, int numDeviceOut,
                        int numSamples, PortSlots& slots)
{
    for (int ch = 0; ch < numDeviceOut; ++ch)
        if (deviceOut[ch] != nullptr)
            std::fill (deviceOut[ch], deviceOut[ch] + numSamples, 0.0f);

    for (size_t i = 0; i < slots.inputs.size(); ++i)
    {
        const int ch = inputChannels[i];
        *slots.inputs[i] = (ch >= 0 && ch < numDeviceIn) ? deviceIn[ch] : nullptr;
    }
    for (size_t i = 0; i < slots.outputs.size(); ++i)
    {
        const int ch = outputChannels[i];
        *slots.outputs[i] = (ch >= 0 && ch < numDeviceOut) ? deviceOut[ch] : nullptr;
    }
}

namespace
{
int channelsNeeded (const std::vector<int>& channels)
{
    int highest = -1;
    for (const int ch : channels)
        highest = std::max (highest, ch);
    return highest + 1;
}

// Two ports on one output channel would overwrite each other.
juce::String duplicateOutput (const std::vector<int>& channels, const PortNames& ports)
{
    std::set<int> seen;
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i] >= 0 && ! seen.insert (channels[i]).second)
            return "io.deviceChannels: output " + juce::String (ports.outputs[i]) + " shares device channel "
                   + juce::String (channels[i]) + " with another output";
    return {};
}
}

DeviceBackend::DeviceBackend() = default;
DeviceBackend::~DeviceBackend() { close(); }

juce::Result DeviceBackend::open (EngineHost& engineHost, const AppConfig& config)
{
    if (host != nullptr)
        return juce::Result::fail ("the device backend is already open");

    const auto names = enginePortNames (engineHost.engineConfig(), engineHost.spatialConfig());
    inputChannels = deviceChannelFor (config.io, names, true);
    outputChannels = deviceChannelFor (config.io, names, false);
    if (const auto clash = duplicateOutput (outputChannels, names); clash.isNotEmpty())
        return juce::Result::fail (clash);
    const int numIn = channelsNeeded (inputChannels), numOut = channelsNeeded (outputChannels);
    if (numOut == 0)
        return juce::Result::fail ("io.deviceChannels maps no output: the device would stay silent");

    // The device passes only the channels it opened, packed in order: open 0..highest mapped,
    // so a device channel number is also the index into the callback's arrays.
    juce::String error = manager.initialise (numIn, numOut, nullptr, false);
    if (error.isEmpty() && config.backend.deviceType.isNotEmpty())
        manager.setCurrentAudioDeviceType (config.backend.deviceType, true);
    auto setup = manager.getAudioDeviceSetup();
    setup.inputDeviceName = config.backend.inputDevice;
    setup.outputDeviceName = config.backend.outputDevice;
    setup.sampleRate = config.backend.sampleRate;
    setup.bufferSize = config.backend.blockSize;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    setup.inputChannels.setRange (0, numIn, true);
    setup.outputChannels.setRange (0, numOut, true);
    if (error.isEmpty())
        error = manager.setAudioDeviceSetup (setup, true);
    auto* device = manager.getCurrentAudioDevice();
    if (error.isNotEmpty() || device == nullptr)
    {
        close();
        return juce::Result::fail (error.isNotEmpty() ? error : juce::String ("no audio device could be opened"));
    }
    if (device->getActiveOutputChannels().countNumberOfSetBits() < numOut
        || device->getActiveInputChannels().countNumberOfSetBits() < numIn)
    {
        close();
        return juce::Result::fail ("the device has fewer channels than io.deviceChannels needs");
    }

    settings = config.backend;
    slots = bindPortSlots (engineHost.engineConfig(), engineHost.spatialConfig(), inputs, outputs);
    sampleRate = device->getCurrentSampleRate();
    const auto built = engineHost.rebuild (sampleRate, device->getCurrentBufferSizeSamples(), false);
    if (built.failed())
    {
        close();
        return built;
    }
    host = &engineHost;
    manager.addAudioCallback (this);
    running = true;
    return juce::Result::ok();
}

void DeviceBackend::close()
{
    if (running)
        manager.removeAudioCallback (this);
    running = false;
    manager.closeAudioDevice();
    host = nullptr;
    slots = {};
}

float combinedLoadPercent (float backendPercent, float engineBlockMs, int frames, double sampleRate)
{
    const double periodMs = frames > 0 && sampleRate > 0 ? 1000.0 * frames / sampleRate : 0.0;
    const float own = periodMs > 0 ? (float) (100.0 * engineBlockMs / periodMs) : 0.0f;
    return std::max (backendPercent, own);
}

RoundTrip estimatedRoundTrip (const BackendStatus& status)
{
    RoundTrip trip;
    if (status.sampleRate <= 0)
        return trip;
    trip.sampleRate = status.sampleRate;
    trip.captureSamples = status.captureLatency;
    trip.processingSamples = status.blockSize;
    trip.playbackSamples = status.playbackLatency;
    return trip;
}

juce::String roundTripText (const RoundTrip& trip)
{
    if (trip.sampleRate <= 0)
        return "Round trip   no engine runs";
    const auto ms = [&] (int samples) { return juce::String (trip.ms (samples), 1); };
    return "Round trip (estimated)   " + ms (trip.totalSamples()) + " ms (" + juce::String (trip.totalSamples()) + " samples) = capture "
           + ms (trip.captureSamples) + " + processing " + ms (trip.processingSamples) + " + playback " + ms (trip.playbackSamples) + " ms";
}

BackendStatus DeviceBackend::status() const
{
    BackendStatus s;
    s.rateSource = "this app (Settings: device, sample rate, block size)";
    s.connecting = false;
    if (auto* device = const_cast<juce::AudioDeviceManager&> (manager).getCurrentAudioDevice())
    {
        s.sampleRate = device->getCurrentSampleRate();
        s.blockSize = device->getCurrentBufferSizeSamples();
        s.bitDepth = device->getCurrentBitDepth();   // AudioDeviceSetup has no bit-depth field: read only
        s.xruns = device->getXRunCount();
        s.captureLatency = device->getInputLatencyInSamples();
        s.playbackLatency = device->getOutputLatencyInSamples();
    }
    s.dspLoadPercent = combinedLoadPercent ((float) (manager.getCpuUsage() * 100.0), engineBlockMs.load(),
                                            lastBlockFrames.load(), s.sampleRate);
    return s;
}

void DeviceBackend::service (double)
{
    if (host == nullptr)
        return;
    const int rate = pendingSampleRate.exchange (0);
    const int block = pendingBlockSize.exchange (0);
    const bool newRate = rate > 0 && (double) rate != sampleRate;
    const bool newBlock = block > 0 && block != host->preparedBlockSize();
    if (! newRate && ! newBlock)
        return;
    if (newRate)
        sampleRate = (double) rate;
    const auto built = host->rebuild (sampleRate, block > 0 ? block : host->preparedBlockSize(), false);
    if (built.failed())
        std::fprintf (stderr, "a3-engine: cannot rebuild for %.0f Hz / %d frames: %s\n", sampleRate, block,
                      built.getErrorMessage().toRawUTF8());
}

void DeviceBackend::audioDeviceIOCallbackWithContext (const float* const* deviceIn, int numIn, float* const* deviceOut,
                                                      int numOut, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto started = std::chrono::steady_clock::now();
    bindDeviceBuffers (inputChannels, outputChannels, deviceIn, numIn, deviceOut, numOut, numSamples, slots);
    host->audioCallback (inputs, outputs, numSamples);
    engineBlockMs.store ((float) std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count(),
                         std::memory_order_relaxed);
    lastBlockFrames.store (numSamples, std::memory_order_relaxed);
}

void DeviceBackend::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    pendingSampleRate.store ((int) device->getCurrentSampleRate());
    pendingBlockSize.store (device->getCurrentBufferSizeSamples());
}

void DeviceBackend::audioDeviceError (const juce::String& message)
{
    std::fprintf (stderr, "a3-engine: audio device error: %s\n", message.toRawUTF8());
}
}
