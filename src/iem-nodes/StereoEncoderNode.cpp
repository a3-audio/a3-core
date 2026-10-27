// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IemProcessorFactories.h"
#include "PluginProcessor.h"

namespace a3::iem
{
namespace
{
constexpr int busOrder = 3;
constexpr float iemOrderThree = 4.0f;   // IEM's choice value for 3rd order (0 = auto)

void setNotifying (juce::AudioProcessorValueTreeState& parameters, const char* id, float value)
{
    auto* parameter = parameters.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

// One pose parameter: its raw value and its range, looked up once off the audio thread.
struct PoseParameter
{
    PoseParameter (juce::AudioProcessorValueTreeState& parameters, const char* id)
        : raw (parameters.getRawParameterValue (id)), range (parameters.getParameterRange (id))
    {
    }

    float legal (float value) const { return range.snapToLegalValue (value); }
    bool differs (float value) const { return legal (value) != raw->load(); }
    void write (float value) const { raw->store (legal (value)); }

    std::atomic<float>* raw;
    juce::NormalisableRange<float> range;
};

// StereoEncoder keeps its direction in a private quaternion that only its
// parameterChanged updates; raw writes alone never reach it (ruling R3). The ids
// are built here, because a juce::String made from a literal allocates.
class PoseWriter
{
public:
    explicit PoseWriter (StereoEncoderAudioProcessor& p)
        : encoder (p),
          azimuth (p.parameters, "azimuth"), elevation (p.parameters, "elevation"),
          roll (p.parameters, "roll"), width (p.parameters, "width")
    {
        // The first notification of each quaternion parameter grows JUCE's
        // listener-iterator vector (ListenerList::callCheckedExcluding); it keeps
        // that capacity, so taking it here leaves the audio thread allocation-free.
        encoder.parameterChanged (azimuthId, azimuth.raw->load());
    }

    // Compared against the raw values, so a pose set any other way still counts.
    void apply (const StereoPose& pose)
    {
        if (azimuth.differs (pose.azimuth) || elevation.differs (pose.elevation) || roll.differs (pose.roll))
        {
            azimuth.write (pose.azimuth);
            elevation.write (pose.elevation);
            roll.write (pose.roll);
            encoder.parameterChanged (azimuthId, azimuth.raw->load());
        }
        if (width.differs (pose.width))
        {
            width.write (pose.width);
            encoder.parameterChanged (widthId, width.raw->load());
        }
    }

private:
    StereoEncoderAudioProcessor& encoder;
    const juce::String azimuthId { "azimuth" }, widthId { "width" };
    PoseParameter azimuth, elevation, roll, width;
};
}

IemNode::Parts createStereoEncoder (int numInputs, int numOutputs)
{
    const auto input = juce::AudioChannelSet::stereo();
    const auto output = juce::AudioChannelSet::ambisonic (busOrder);
    if (numInputs != input.size() || numOutputs != output.size())
        return {};

    auto processor = std::make_unique<StereoEncoderAudioProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (input);
    layout.outputBuses.add (output);
    if (! processor->setBusesLayout (layout))
        return {};

    auto* encoder = processor.get();
    setNotifying (encoder->parameters, "orderSetting", iemOrderThree);
    setNotifying (encoder->parameters, "useSN3D", 1.0f);
    setNotifying (encoder->parameters, "highQuality", 1.0f);

    auto writer = std::make_shared<PoseWriter> (*encoder);
    IemNode::Parts parts { std::move (processor), &encoder->parameters,
                           [encoder] { return encoder->oscParameterInterface.getOSCReceiver().isConnected(); } };
    parts.setStereoPose = [writer] (const StereoPose& pose) { writer->apply (pose); };
    return parts;
}
}
