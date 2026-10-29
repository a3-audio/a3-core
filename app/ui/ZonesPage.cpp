// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ZonesPage.h"
#include "DecoderSummary.h"
#include <algorithm>

namespace a3app
{
namespace
{
juce::String degrees (float value)
{
    return value == std::round (value) ? juce::String (juce::roundToInt (value)) : juce::String (value, 1);
}

juce::String speakerList (const std::vector<a3::Loudspeaker>& speakers)
{
    juce::StringArray rows;
    for (const auto& s : speakers)
        rows.add ((s.imaginary ? juce::String ("imaginary") : "ch " + juce::String (s.channel)) + " az " + degrees (s.azimuth)
                  + " el " + degrees (s.elevation));
    return rows.isEmpty() ? juce::String ("no loudspeaker table in the file") : rows.joinIntoString ("  ·  ");
}

juce::String crossoverText (const a3::DecoderSummary& s)
{
    if (s.lowPassHz == s.highPassHz)
        return "crossover " + degrees (s.lowPassHz) + " Hz";
    return "sub below " + degrees (s.lowPassHz) + " Hz, tops above " + degrees (s.highPassHz) + " Hz";
}

// Three lines: the zone and its directory; tool, date, order, weights, sub and crossover; the loudspeakers.
juce::String zoneSummary (a3::SpeakerZone zone, const juce::File& directory)
{
    const juce::String name = a3::zoneName (zone);
    const auto head = name.toUpperCase() + "   " + directory.getFullPathName();
    const auto decoderFile = directory.getChildFile (name + ".decoder.json");
    juce::String error;
    const auto summary = a3::decoderSummary (decoderFile.loadFileAsString(),
                                             juce::JSON::parse (directory.getChildFile (name + ".parameters.json").loadFileAsString()), error);
    if (! summary)
        return head + "\n" + decoderFile.getFullPathName() + ": " + error;
    const auto made = summary->madeWith.isNotEmpty() ? summary->madeWith + ", " + summary->exportedOn : juce::String ("tool not named");
    return head + "\n" + made + "  ·  order " + juce::String (summary->order) + "  ·  weights " + summary->weights + "  ·  "
           + (summary->subChannel > 0 ? "sub ch " + juce::String (summary->subChannel) : juce::String ("no sub")) + "  ·  "
           + crossoverText (*summary) + "\n" + speakerList (summary->loudspeakers);
}
}

class ZonesPage::Model final : public juce::ListBoxModel
{
public:
    explicit Model (ZonesPage& p) : page (p) {}
    int getNumRows() override { return page.rowCount(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= page.rowCount())
            return;
        if (selected)
            g.fillAll (palette::lifted);
        const auto& directory = page.directories[(size_t) row];
        g.setFont ((float) height * 0.5f);
        g.setColour (selected ? palette::text : palette::textMuted);
        g.drawText (directory.getFileName(), width / 40, 0, width / 3, height, juce::Justification::centredLeft);
        g.setColour (palette::textMuted);
        g.drawText (directory.getParentDirectory().getFullPathName(), width / 3 + width / 20, 0, width * 2 / 3 - width / 20, height,
                    juce::Justification::centredLeft);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { page.choose (row); }

private:
    ZonesPage& page;
};

ZonesPage::ZonesPage (HostView& v, std::vector<juce::File> choices)
    : view (v), directories (std::move (choices)), model (std::make_unique<Model> (*this)), list ("zones", model.get())
{
    const auto current = view.config().spatialDirectory;
    if (std::find (directories.begin(), directories.end(), current) == directories.end())
        directories.insert (directories.begin(), current);
    look::caption (title);
    look::failure (message);
    title.setText ("ZONES", juce::dontSendNotification);
    look::help ("The spatial data: decoders, layouts and zone stages. Installed variants and your own "
                "(~/.config/a3/engine/spatial). Layouts are edited on SPEAKERS. Save restarts the engine.",
                { &title, &list, &openSpeakers });
    for (auto* summary : { &mainSummary, &boothSummary })
    {
        look::value (*summary);
        summary->setJustificationType (juce::Justification::topLeft);
        summary->setMinimumHorizontalScale (1.0f);
    }
    openSpeakers.onClick = [this] { if (onOpenSpeakers) onOpenSpeakers(); };
    list.setRowHeight (juce::jmax (28, 640 / 14));
    look::caption (headphonesCaption);
    headphonesCaption.setText ("PHONES HEADPHONE EQ", juce::dontSendNotification);
    headphones.addItem ("off", 1);
    int id = 2;
    for (const auto& model : a3::headphoneEqModels())
        headphones.addItem (model, id++);
    headphones.onChange = [this] { chooseHeadphoneEq (headphones.getSelectedItemIndex()); };
    for (auto* child : std::initializer_list<juce::Component*> { &title, &message, &list, &headphonesCaption, &headphones,
                                                                 &mainSummary, &boothSummary, &openSpeakers })
        addAndMakeVisible (child);
    refresh();
}

ZonesPage::~ZonesPage() { list.setModel (nullptr); }

int ZonesPage::selectedRow() const
{
    const auto it = std::find (directories.begin(), directories.end(), view.config().spatialDirectory);
    return it == directories.end() ? -1 : (int) (it - directories.begin());
}

void ZonesPage::choose (int row)
{
    if (row < 0 || row >= rowCount())
        return;
    juce::String error;
    const auto edited = withSpatialDirectory (view.config(), directories[(size_t) row], error);
    look::say (message, edited ? juce::String() : error);
    if (edited && ! sameConfig (*edited, view.config()))
        view.edit (*edited);
    refresh();
}

void ZonesPage::chooseHeadphoneEq (int item)
{
    const auto& models = a3::headphoneEqModels();
    if (item < 0 || item > (int) models.size())
        return;
    juce::String error;
    const auto edited = withHeadphoneEq (view.config(), item == 0 ? juce::String ("off") : models[(size_t) item - 1], error);
    look::say (message, edited ? juce::String() : error);
    if (edited && ! sameConfig (*edited, view.config()))
        view.edit (*edited);
    refresh();
}

juce::String ZonesPage::summaryText (a3::SpeakerZone zone) const
{
    return (zone == a3::SpeakerZone::main ? mainSummary : boothSummary).getText();
}

// The running directory and when its zone files last changed: a variant saved in place shows after the restart.
juce::String ZonesPage::summaryKey() const
{
    const auto directory = view.running().spatialDirectory;
    auto key = directory.getFullPathName();
    for (const auto* name : { "main.decoder.json", "main.parameters.json", "booth.decoder.json", "booth.parameters.json" })
        key << "|" << directory.getChildFile (name).getLastModificationTime().toMilliseconds();
    return key;
}

// What plays: the running directory's files, read again only when they change.
void ZonesPage::summarise()
{
    summarisedFrom = view.running().spatialDirectory;
    summarisedKey = summaryKey();
    look::say (mainSummary, zoneSummary (a3::SpeakerZone::main, summarisedFrom));
    look::say (boothSummary, zoneSummary (a3::SpeakerZone::booth, summarisedFrom));
}

void ZonesPage::refresh()
{
    if (summaryKey() != summarisedKey)
        summarise();
    const auto choice = a3::headphoneEqChoice (view.config().engine.phonesHeadphoneEq);
    headphones.setSelectedItemIndex (choice.value_or (0), juce::dontSendNotification);
    const int row = selectedRow();
    if (row >= 0)
        list.selectRow (row, true, true);
    else
        list.deselectAllRows();
}

void ZonesPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void ZonesPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = look::rowHeight (*this);
    list.setRowHeight (row);
    title.setBounds (area.removeFromTop (row));
    message.setBounds (area.removeFromBottom (row));
    auto eqRow = area.removeFromBottom (row);
    headphonesCaption.setBounds (eqRow.removeFromLeft (eqRow.getWidth() / 3));
    headphones.setBounds (eqRow.removeFromLeft (eqRow.getWidth() / 2));
    openSpeakers.setBounds (eqRow.reduced (row / 8, 0));
    const auto summaryFont = juce::FontOptions ((float) row * 0.32f);
    for (auto* summary : { &boothSummary, &mainSummary })
    {
        summary->setFont (summaryFont);
        summary->setBounds (area.removeFromBottom (row * 2));   // five lines of the summary at its font
    }
    list.setBounds (area);
}
}
