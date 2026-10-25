// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <functional>
#include <memory>

namespace a3
{
enum class IemKind { multiEncoder, simpleDecoder, binauralDecoder, stereoEncoder };

// Where a StereoEncoder puts its stereo source, in degrees.
struct StereoPose
{
    float azimuth = 0, elevation = 0, roll = 0, width = 60;
};

// One IEM plug-in, compiled unchanged from third_party/IEMPluginSuite, hosted
// directly: no plug-in format, no host negotiation, IEM's OSC receiver closed.
class IemNode
{
public:
    // Builds the processor and sets its bus layout explicitly (no host negotiation):
    // multiEncoder: numInputs discrete in, ambisonic order-3 out (16); decoders: 16 in, numOutputs out;
    // stereoEncoder: 2 in, ambisonic order-3 SN3D out (16), high quality.
    // Returns nullptr when the processor refuses that layout.
    static std::unique_ptr<IemNode> create (IemKind, int numInputs, int numOutputs);
    virtual ~IemNode();

    juce::Result setParameter (const juce::String& id, float value);   // off the audio thread; unknown id → failure
    juce::Result setParameters (const juce::var& idToValue);          // a JSON object {id: value}
    void prepare (double sampleRate, int blockSize);  // allocates; then one silent warm-up block
    int preparedBlockSize() const;
    void process (juce::AudioBuffer<float>& io);      // io has max(in, out) channels, numSamples <= preparedBlockSize()
    // stereoEncoder only, on the audio thread: takes effect from the next process();
    // a pose equal to the current one changes nothing. A no-op on the other kinds.
    void setStereoPose (const StereoPose&);
    std::atomic<float>* rawParameter (const juce::String& id);   // APVTS raw value, for audio-thread writes
    float legalValue (const juce::String& id, float value) const; // clamp + snap to the parameter's range
    bool listensForOsc() const;                       // IEM's own OSC receiver connected?
    // SimpleDecoder only; IEM's own config format. The zone's parameters own the sub:
    // swMode/swChannel set before a load survive it, even when the loaded config
    // has its own SubwooferChannel (IEM would otherwise overwrite them on pickup).
    juce::Result loadDecoderConfig (const juce::String& json);
    juce::MemoryBlock state();                        // getStateInformation (null tests)
    juce::AudioProcessor& processor();

    // What a plug-in-specific factory hands over; the IEM types stay in its own TU.
    struct Parts
    {
        std::unique_ptr<juce::AudioProcessor> processor;
        juce::AudioProcessorValueTreeState* parameters = nullptr;
        std::function<bool()> listensForOsc;
        std::function<juce::Result (const juce::String&)> loadDecoderConfig;   // SimpleDecoder only
        std::function<void (const StereoPose&)> setStereoPose;                // StereoEncoder only
    };

private:
    explicit IemNode (Parts);
    juce::RangedAudioParameter* findParameter (const juce::String& id) const;
    int ioChannels() const;
    void warmUp();
    juce::Result validateParameters (const juce::DynamicObject& idToValue) const;

    Parts parts;
    juce::MidiBuffer noMidi;
    int blockSize = 0;
};
}
