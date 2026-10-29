// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AllRadDesign.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace a3
{
namespace
{
constexpr float thirdOrder = 2.0f;   // AllRADecoder's decoderOrder choice: 0 = 1st ... 2 = 3rd

void setChoice (juce::AudioProcessorValueTreeState& parameters, const char* id, float value)
{
    auto* parameter = parameters.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

std::optional<AmbisonicWeights> weightsNamed (const juce::String& name)
{
    if (name.equalsIgnoreCase ("none"))
        return AmbisonicWeights::basic;
    if (name.equalsIgnoreCase ("maxrE"))
        return AmbisonicWeights::maxrE;
    if (name.equalsIgnoreCase ("inPhase"))
        return AmbisonicWeights::inPhase;
    return std::nullopt;
}
}

struct AllRadDesigner::Impl
{
    AllRADecoderAudioProcessor processor;
};

AllRadDesigner::AllRadDesigner() : impl (std::make_unique<Impl>())
{
    setChoice (impl->processor.parameters, "decoderOrder", thirdOrder);
}

AllRadDesigner::~AllRadDesigner() = default;

// One undo step, as IEM's own loadConfiguration replaces the table.
void AllRadDesigner::setLoudspeakers (const std::vector<Loudspeaker>& speakers)
{
    auto& processor = impl->processor;
    auto& table = processor.getLoudspeakersValueTree();
    processor.undoManager.beginNewTransaction();
    table.removeAllChildren (&processor.undoManager);
    for (const auto& s : speakers)
        table.appendChild (ConfigurationHelper::createElement (s.azimuth, s.elevation, s.radius, s.channel, s.imaginary, s.gain),
                           &processor.undoManager);
}

void AllRadDesigner::setWeights (AmbisonicWeights weights)
{
    setChoice (impl->processor.parameters, "weights", (float) static_cast<int> (weights));
}

juce::Result AllRadDesigner::load (const juce::String& configurationJson)
{
    juce::var parsed;
    if (const auto parse = juce::JSON::parse (configurationJson, parsed); parse.failed())
        return juce::Result::fail ("not valid JSON: " + parse.getErrorMessage());

    auto& processor = impl->processor;
    juce::ValueTree table ("Loudspeakers");
    if (const auto result = ConfigurationHelper::parseVarForLoudspeakerLayout (parsed, table, nullptr); result.failed())
        return result;

    processor.undoManager.beginNewTransaction();
    auto& loaded = processor.getLoudspeakersValueTree();
    loaded.removeAllChildren (&processor.undoManager);
    while (table.getNumChildren() > 0)
    {
        auto speaker = table.getChild (0);
        table.removeChild (0, nullptr);
        loaded.appendChild (speaker, &processor.undoManager);
    }
    if (const auto weights = weightsNamed (parsed["Decoder"]["Weights"].toString()))
        setWeights (*weights);
    return juce::Result::ok();
}

std::optional<juce::String> AllRadDesigner::design (juce::String& error)
{
    auto& processor = impl->processor;
    if (const auto result = processor.calculateDecoder(); result.failed())
    {
        // calculateDecoder only says "Layout not ready!"; the reason is in the message for the editor.
        error = processor.messageToEditor.text.isNotEmpty() ? processor.messageToEditor.text : result.getErrorMessage();
        return std::nullopt;
    }
    juce::TemporaryFile file (".json");
    processor.saveConfigurationToFile (file.getFile());
    if (! file.getFile().existsAsFile())
    {
        error = processor.messageToEditor.headline + " " + processor.messageToEditor.text;
        return std::nullopt;
    }
    return file.getFile().loadFileAsString();
}

std::unique_ptr<juce::AudioProcessorEditor> AllRadDesigner::createEditor()
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (impl->processor.createEditor());
    // IEM's footer opens the processor's OSC receiver; the window's never listens (see listensForOsc).
    for (auto* child : editor->getChildren())
        if (dynamic_cast<OSCFooter*> (child) != nullptr)
        {
            child->setVisible (false);
            child->setEnabled (false);
        }
    return editor;
}

void AllRadDesigner::setNoiseHooks (std::function<void (int)> channel, std::function<void (float, float)> direction)
{
    impl->processor.a3NoiseHook = std::move (channel);
    impl->processor.a3AmbisonicNoiseHook = std::move (direction);
}
void AllRadDesigner::playNoise (int channel) { impl->processor.playNoiseBurst (channel); }
void AllRadDesigner::playAmbisonicNoise (float azimuth, float elevation) { impl->processor.playAmbisonicNoiseBurst (azimuth, elevation); }

bool AllRadDesigner::listensForOsc() const { return impl->processor.oscParameterInterface.getOSCReceiver().isConnected(); }

bool AllRadDesigner::showsOscControls (const juce::Component& editor)
{
    for (auto* child : editor.getChildren())
        if (dynamic_cast<OSCFooter*> (child) != nullptr && (child->isVisible() || child->isEnabled()))
            return true;
    return false;
}

std::optional<juce::String> designDecoder (const std::vector<Loudspeaker>& speakers, AmbisonicWeights weights, juce::String& error)
{
    AllRadDesigner designer;
    designer.setLoudspeakers (speakers);
    designer.setWeights (weights);
    return designer.design (error);
}
}
