// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpeakersPage.h"
#include "ConfigEdits.h"

namespace a3app
{
namespace
{
constexpr int editorMinWidth = 1000, editorMinHeight = 600;   // IEM's editor's own resize limits
constexpr int highestSubChannel = 31;                         // the engine's output limit (Layout.h)
constexpr double lowestCrossover = 20.0, highestCrossover = 300.0;   // SimpleDecoder's range
constexpr int noSubItem = 1;

double numberOr (const juce::var& object, const char* id, double fallback)
{
    const auto value = object.getProperty (id, juce::var());
    return (value.isInt() || value.isInt64() || value.isDouble()) ? (double) value : fallback;
}

// SimpleDecoder's "weights" choice for what the designed file says (it applies its own parameter).
double simpleDecoderWeights (const juce::String& fileWeights)
{
    if (fileWeights.equalsIgnoreCase ("maxrE"))
        return 1.0;
    if (fileWeights.equalsIgnoreCase ("inPhase"))
        return 2.0;
    return 0.0;
}

void sayOutcome (juce::Label& label, const juce::String& text, bool ok)
{
    if (ok)
        look::value (label);
    else
        look::failure (label);
    look::say (label, text);
}
}

SpeakersPage::SpeakersPage (HostView& v, juce::File root) : view (v), userRoot (std::move (root))
{
    look::caption (title);
    look::caption (subCaption);
    look::caption (crossoverCaption);
    look::failure (message);
    look::caption (note);
    title.setText ("SPEAKERS", juce::dontSendNotification);
    subCaption.setText ("Sub", juce::dontSendNotification);
    crossoverCaption.setText ("Crossover", juce::dontSendNotification);
    note.setText ("IEM's AllRADecoder at order 3. Noise: 1 s of pink noise in the running engine, through the decoder that "
                  "runs now, before MASTER/BOOTH and the gate (Alt: from that direction). SAVE TO ZONE writes your own variant (~/.config/a3/engine/spatial); "
                  "Save restarts the engine with it.", juce::dontSendNotification);
    note.setMinimumHorizontalScale (1.0f);

    for (auto* key : { &mainKey, &boothKey })
    {
        key->setRadioGroupId (1);
        key->setClickingTogglesState (false);
    }
    mainKey.onClick = [this] { showZone (a3::SpeakerZone::main); };
    boothKey.onClick = [this] { showZone (a3::SpeakerZone::booth); };
    save.onClick = [this] { saveToZone(); };

    sub.addItem ("no sub", noSubItem);
    for (int channel = 1; channel <= highestSubChannel; ++channel)
        sub.addItem ("ch " + juce::String (channel), noSubItem + channel);
    sub.onChange = [this] { setSubChannel (sub.getSelectedId() - noSubItem); };
    crossover.setRange (lowestCrossover, highestCrossover, 1.0);
    crossover.setTextValueSuffix (" Hz");
    crossover.onValueChange = [this] { setCrossover ((float) crossover.getValue()); };

    designer.setNoiseHooks ([this] (int channel)
    {
        NoiseRequest burst;
        burst.zone = shown;
        burst.channel = channel;
        view.playNoise (burst);
    },
    [this] (float azimuth, float elevation)
    {
        NoiseRequest burst;
        burst.zone = shown;
        burst.ambisonic = true;
        burst.azimuth = azimuth;
        burst.elevation = elevation;
        view.playNoise (burst);
    });

    for (auto* child : std::initializer_list<juce::Component*> { &title, &mainKey, &boothKey, &save, &subCaption, &sub,
                                                                 &crossoverCaption, &crossover, &message, &note, &viewport })
        addAndMakeVisible (child);
    load();
}

SpeakersPage::~SpeakersPage()
{
    viewport.setViewedComponent (nullptr, false);
    iemEditor.reset();   // before the designer it edits
}

void SpeakersPage::showZone (a3::SpeakerZone zone)
{
    shown = zone;
    load();
}

// The pending config's files for the shown zone into IEM's processor, and a fresh editor on it (IEM's editor
// picks a new table up on its own timer; a new one shows it at once).
void SpeakersPage::load()
{
    loadedFrom = view.config().spatialDirectory;
    const juce::String name = a3::zoneName (shown);
    const auto decoderFile = loadedFrom.getChildFile (name + ".decoder.json");
    parameters = juce::JSON::parse (loadedFrom.getChildFile (name + ".parameters.json").loadFileAsString());
    if (! parameters.isObject())
        parameters = juce::var (new juce::DynamicObject());

    const auto loaded = decoderFile.existsAsFile() ? designer.load (decoderFile.loadFileAsString())
                                                   : juce::Result::fail ("there is no such file");
    if (loaded.failed())
        designer.setLoudspeakers ({});
    sayOutcome (message, loaded.failed() ? decoderFile.getFullPathName() + ": " + loaded.getErrorMessage() : juce::String(), loaded.wasOk());

    viewport.setViewedComponent (nullptr, false);
    iemEditor = designer.createEditor();
    viewport.setViewedComponent (iemEditor.get(), false);
    mainKey.setToggleState (shown == a3::SpeakerZone::main, juce::dontSendNotification);
    boothKey.setToggleState (shown == a3::SpeakerZone::booth, juce::dontSendNotification);
    showParameters();
    resized();
}

void SpeakersPage::showParameters()
{
    subChannel = juce::roundToInt (numberOr (parameters, "swMode", 0)) == 1 ? juce::roundToInt (numberOr (parameters, "swChannel", 1)) : 0;
    crossoverHz = (float) numberOr (parameters, "lowPassFrequency", crossoverHz);
    sub.setSelectedId (noSubItem + juce::jlimit (0, highestSubChannel, subChannel), juce::dontSendNotification);
    crossover.setValue (crossoverHz, juce::dontSendNotification);
}

void SpeakersPage::setSubChannel (int channel)
{
    subChannel = juce::jlimit (0, highestSubChannel, channel);
    sub.setSelectedId (noSubItem + subChannel, juce::dontSendNotification);
}

void SpeakersPage::setCrossover (float hz)
{
    crossoverHz = (float) juce::jlimit (lowestCrossover, highestCrossover, (double) hz);
    crossover.setValue (crossoverHz, juce::dontSendNotification);
}

juce::var SpeakersPage::editedParameters() const
{
    auto edited = parameters.clone();
    auto* values = edited.getDynamicObject();
    values->setProperty ("swMode", subChannel > 0 ? 1.0 : 0.0);
    if (subChannel > 0)
        values->setProperty ("swChannel", (double) subChannel);
    values->setProperty ("lowPassFrequency", (double) crossoverHz);
    values->setProperty ("highPassFrequency", (double) crossoverHz);
    return edited;
}

void SpeakersPage::saveToZone()
{
    if (userRoot == juce::File())
        return sayOutcome (message, "no user directory to save a variant to", false);
    juce::String error;
    const auto designed = designer.design (error);
    if (! designed)
        return sayOutcome (message, "IEM: " + error, false);

    auto values = editedParameters();
    values.getDynamicObject()->setProperty ("weights", simpleDecoderWeights (juce::JSON::parse (*designed)["Decoder"]["Weights"].toString()));
    const auto edited = withZoneDecoder (view.config(), shown, *designed, values, userRoot, error);
    if (! edited)
        return sayOutcome (message, error, false);
    view.edit (*edited);
    loadedFrom = edited->spatialDirectory;   // what the editor shows is what was written
    parameters = values;
    sayOutcome (message, juce::String (a3::zoneName (shown)).toUpperCase() + " saved to " + loadedFrom.getFullPathName()
                             + ". Save restarts the engine with it.", true);
}

void SpeakersPage::refresh()
{
    if (view.config().spatialDirectory != loadedFrom)
        load();
}

void SpeakersPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void SpeakersPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 80);
    const int row = look::rowHeight (*this);
    auto top = area.removeFromTop (row);
    title.setBounds (top.removeFromLeft (top.getWidth() / 6));
    mainKey.setBounds (top.removeFromLeft (top.getWidth() / 5).reduced (2));
    boothKey.setBounds (top.removeFromLeft (top.getWidth() / 4).reduced (2));
    save.setBounds (top.removeFromRight (top.getWidth() / 2).reduced (2));
    auto settings = area.removeFromTop (row);
    subCaption.setBounds (settings.removeFromLeft (settings.getWidth() / 8));
    sub.setBounds (settings.removeFromLeft (settings.getWidth() / 5).reduced (2));
    crossoverCaption.setBounds (settings.removeFromLeft (settings.getWidth() / 5));
    crossover.setBounds (settings.reduced (2));
    message.setBounds (area.removeFromTop (row));
    note.setFont (juce::FontOptions ((float) row * 0.3f));
    note.setBounds (area.removeFromBottom (row));
    viewport.setBounds (area);
    if (iemEditor != nullptr)
        iemEditor->setSize (juce::jmax (editorMinWidth, viewport.getMaximumVisibleWidth()),
                            juce::jmax (editorMinHeight, viewport.getMaximumVisibleHeight()));
}
}
