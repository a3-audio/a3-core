// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace a3
{
// One row of IEM's AllRADecoder table, as its configuration files carry it.
struct Loudspeaker
{
    float azimuth = 0.0f, elevation = 0.0f, radius = 1.0f;
    bool imaginary = false;
    int channel = 1;   // 1-based decoder output channel
    float gain = 1.0f;
};

// AllRADecoder's "weights" choice, in its order.
enum class AmbisonicWeights { basic = 0, maxrE = 1, inPhase = 2 };

// IEM's AllRADecoder processor, compiled unchanged from third_party/IEMPluginSuite and never given
// audio: the window edits a layout in IEM's own editor, and the decoder file is what IEM's
// calculateDecoder and saveConfigurationToFile make of it. The order is fixed at 3 (the engine's bus).
class AllRadDesigner
{
public:
    AllRadDesigner();
    ~AllRadDesigner();

    void setLoudspeakers (const std::vector<Loudspeaker>&);
    void setWeights (AmbisonicWeights);
    // An AllRADecoder file's LoudspeakerLayout (and its Decoder's Weights, when it names them): IEM's
    // own parser, so its refusal comes back verbatim.
    juce::Result load (const juce::String& configurationJson);

    // calculateDecoder, then saveConfigurationToFile (decoder and layout): the file's text, or nullopt
    // and IEM's message (an improper layout's, e.g. "ERROR 2: There are less than 4 loudspeakers!").
    std::optional<juce::String> design (juce::String& error);

    // IEM's AllRADecoderAudioProcessorEditor on this processor; it must not outlive the designer.
    std::unique_ptr<juce::AudioProcessorEditor> createEditor();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// One call of the above: layout + weights in, IEM's verbatim JSON out.
std::optional<juce::String> designDecoder (const std::vector<Loudspeaker>&, AmbisonicWeights, juce::String& error);
}
