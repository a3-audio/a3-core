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

// IEM's HammerAitovGrid (the energy map's front view) fills what lies outside its ellipse with a fixed 0xff2d2d2d. As
// a component effect (JUCE's ImageEffectFilter, applied to the component's image before it is drawn), that grey becomes
// `ground`, keeping each pixel's alpha (the ellipse's antialiased edge). IEM's code is untouched.
class RecolourGround final : public juce::ImageEffectFilter
{
public:
    explicit RecolourGround (juce::Colour toUse) : ground (toUse) {}
    void applyEffect (juce::Image& image, juce::Graphics& g, float scaleFactor, float alpha) override
    {
        static const juce::Colour iemGround { 0xff2d2d2d };
        juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                if (c.getAlpha() > 0 && c.withAlpha ((juce::uint8) 255) == iemGround)
                    pixels.setPixelColour (x, y, ground.withAlpha (c.getAlpha()));
            }
        g.setOpacity (alpha);
        g.drawImage (image, juce::Rectangle<float> ((float) image.getWidth(), (float) image.getHeight()) / scaleFactor);
    }

private:
    juce::Colour ground;
};

// IEM's editor as the window hosts it: it draws no ground of its own (IEM fills its LaF's ClBackground, a lighter
// grey than the window's), so it sits on the page's surface; the 3D view clears to `ground` (the patch's a3Ground) and
// the energy map's grey outside its ellipse becomes `ground` (RecolourGround). The title bar (IEM's I/O widgets, nothing
// the window uses) is hidden and its room given to the table and the 3D view. The energy map is not buffered to an
// image: scaled by the page's transform, its cached image was cut at its right edge (2026-10-31).
class HostedAllRadEditor final : public AllRADecoderAudioProcessorEditor
{
public:
    HostedAllRadEditor (AllRADecoderAudioProcessor& p, juce::AudioProcessorValueTreeState& v, juce::Colour ground)
        : AllRADecoderAudioProcessorEditor (p, v), recolour (ground)
    {
        recolourGrids (*this);
        for (auto* child : getChildren())
            if (auto* view = dynamic_cast<LoudspeakerVisualizer*> (child))
                view->a3Ground = ground;
        unbuffer (*this);
    }
    ~HostedAllRadEditor() override { unsetEffects (*this); }
    void paint (juce::Graphics&) override {}

    // IEM's layout, then the title's band (its 60 px and the 10 under it, AllRADecoderAudioProcessorEditor::resized)
    // given to what hangs from the top: the layout group, the table and the 3D view.
    void resized() override
    {
        AllRADecoderAudioProcessorEditor::resized();
        constexpr int titleBand = 60 + 10;
        for (auto* child : getChildren())
        {
            const auto bounds = child->getBounds();
            if (bounds.getY() == 0 && bounds.getHeight() == titleBand - 10 && dynamic_cast<OSCFooter*> (child) == nullptr)
            {
                child->setVisible (false);   // the title bar
                continue;
            }
            const bool hangsFromTheTop = bounds.getY() == titleBand || dynamic_cast<LoudspeakerVisualizer*> (child) != nullptr
                                         || dynamic_cast<LoudspeakerTableComponent*> (child) != nullptr;
            if (hangsFromTheTop && bounds.getY() >= titleBand)
                child->setBounds (bounds.withTop (bounds.getY() - titleBand));
        }
    }

private:
    // IEM buffers the energy map and its grid to images; scaled by the page's transform, an image made at the unscaled
    // size was cut at its right edge. Nothing of the hosted editor is buffered.
    static void unbuffer (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            child->setBufferedToImage (false);
            unbuffer (*child);
        }
    }

    void recolourGrids (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (dynamic_cast<HammerAitovGrid*> (child) != nullptr)
                child->setComponentEffect (&recolour);
            recolourGrids (*child);
        }
    }
    static void unsetEffects (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            child->setComponentEffect (nullptr);
            unsetEffects (*child);
        }
    }
    RecolourGround recolour;
};

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

std::unique_ptr<juce::AudioProcessorEditor> AllRadDesigner::createEditor (juce::Colour ground)
{
    auto editor = std::make_unique<HostedAllRadEditor> (impl->processor, impl->processor.parameters, ground);
    editor->setOpaque (false);
    // IEM's footer opens the processor's OSC receiver; the window's never listens (see listensForOsc).
    for (auto* child : editor->getChildren())
        if (dynamic_cast<OSCFooter*> (child) != nullptr)
        {
            child->setVisible (false);
            child->setEnabled (false);
        }
    return std::unique_ptr<juce::AudioProcessorEditor> (std::move (editor));
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
