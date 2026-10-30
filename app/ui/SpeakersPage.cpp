// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpeakersPage.h"
#include "ConfigEdits.h"

namespace a3app
{
namespace
{
// IEM's AllRADecoder editor lays out in its own fixed pixels: margins and the table's column take a fixed
// width, the 3D view and the energy map share what is left. We give it a narrower width than its own minimum
// (the 3D view a share of the table's column) and a height that fills the page, then scale it to the page.
constexpr int iemTableColumn = 420;
constexpr int iemFixedWidth = 2 * 30 + iemTableColumn + 20;   // margins, the table's column, the gap (its resized())
constexpr float viewerShareOfTable = 0.6f;
constexpr int editorWidth = iemFixedWidth + (int) (iemTableColumn * viewerShareOfTable);
constexpr int editorMinHeight = 600;   // IEM's own minimum: below it the table's column runs out
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
    title.setText ("Zone", juce::dontSendNotification);
    subCaption.setText ("Sub", juce::dontSendNotification);
    crossoverCaption.setText ("Crossover", juce::dontSendNotification);

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
                                                                 &crossoverCaption, &crossover, &message })
        addAndMakeVisible (child);
    load();
}

SpeakersPage::~SpeakersPage()
{
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
    loadedFrom = zoneSpatialDirectory (view.config(), shown);
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

    iemEditor = designer.createEditor();
    look::stripTooltips (*iemEditor);   // IEM's own TooltipWindow then has nothing to show
    addAndMakeVisible (*iemEditor);
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
    loadedFrom = zoneSpatialDirectory (*edited, shown);   // what the editor shows is what was written
    parameters = values;
    sayOutcome (message, juce::String (a3::zoneName (shown)).toUpperCase() + " saved to " + loadedFrom.getFullPathName()
                             + ". Save restarts the engine with it.", true);
}

void SpeakersPage::refresh()
{
    if (zoneSpatialDirectory (view.config(), shown) != loadedFrom)
        load();
    else if (iemEditor != nullptr)
        look::stripTooltips (*iemEditor);   // IEM's table makes its rows' buttons (with tips) as it fills
}

void SpeakersPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

// The bar in the rows of AUDIO and I/O: the title row (title, the zone keys, SAVE TO ZONE at the right), then one
// row per setting, the caption in the first quarter and the control on the rest, the message, a gap, the editor.
void SpeakersPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40, getWidth() / 80);
    const int row = look::rowHeight (*this);
    const int gap = juce::jmax (1, row / 14);
    auto top = area.removeFromTop (row);
    title.setBounds (top.removeFromLeft (top.getWidth() / 4));
    const int key = top.getWidth() / 4;
    mainKey.setBounds (top.removeFromLeft (key).reduced (gap));
    boothKey.setBounds (top.removeFromLeft (key).reduced (gap));
    save.setBounds (top.removeFromRight (key * 3 / 2).reduced (gap));
    const auto line = [&] (juce::Label& caption, juce::Component& control)
    {
        auto r = area.removeFromTop (row);
        caption.setBounds (r.removeFromLeft (r.getWidth() / 4));
        control.setBounds (r.reduced (gap));
    };
    line (subCaption, sub);
    line (crossoverCaption, crossover);
    crossover.setTextBoxStyle (juce::Slider::TextBoxRight, false, getWidth() / 6, row - 2 * gap);
    message.setBounds (area.removeFromTop (row));
    area.removeFromTop (row / 4);
    placeEditor (area);
}

// IEM's editor at its own size, scaled (aspect kept) to fill `area`'s width, or its height when that is
// the tighter. All of it stays inside the page: its 3D view is a native OpenGL window that nothing clips.
void SpeakersPage::placeEditor (juce::Rectangle<int> area)
{
    if (iemEditor == nullptr || area.isEmpty())
        return;
    const auto widthScale = (float) area.getWidth() / (float) editorWidth;
    const int height = juce::jmax (editorMinHeight, (int) ((float) area.getHeight() / widthScale));
    const auto scale = juce::jmin (widthScale, (float) area.getHeight() / (float) height);
    const auto left = (float) area.getX() + ((float) area.getWidth() - (float) editorWidth * scale) * 0.5f;
    const auto transform = juce::AffineTransform::scale (scale).translated (left, (float) area.getY());
    if (transform != iemEditor->getTransform())
    {
        iemEditor->setTransform (transform);
        // A transform alone moves no child: the 3D view's OpenGL window follows only a move or a resize of
        // its own, so the editor is laid out once more at another height.
        iemEditor->setBounds (0, 0, editorWidth, height + 1);
    }
    iemEditor->setBounds (0, 0, editorWidth, height);
}
}
