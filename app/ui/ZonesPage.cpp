// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ZonesPage.h"
#include "ConfigEdits.h"
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

bool holdsAVariant (const juce::File& directory) { return directory.getChildFile ("main.decoder.json").existsAsFile(); }

void confirmWithBox (const juce::String& question, std::function<void()> onYes)
{
    const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                             .withTitle ("Delete variant").withMessage (question).withButton ("DELETE").withButton ("Cancel");
    juce::AlertWindow::showAsync (options, [onYes = std::move (onYes)] (int result) { if (result == 1 && onYes) onYes(); });
}

void askNameWithBox (const juce::String& current, std::function<void (const juce::String&)> onName)
{
    auto* box = new juce::AlertWindow ("Rename variant", "A new name for " + current
                                           + " (no slash, not starting with a dot, not one that exists).",
                                       juce::MessageBoxIconType::NoIcon);
    box->addTextEditor ("name", current);
    box->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
    box->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    box->enterModalState (true, juce::ModalCallbackFunction::create ([box, onName = std::move (onName)] (int result)
    {
        if (result == 1 && onName)
            onName (box->getTextEditorContents ("name"));
    }), true);   // deleted once dismissed, after this callback
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
        g.setFont ((float) height * 0.45f);
        g.setColour (selected ? palette::text : palette::textMuted);
        g.drawText (directory.getFileName(), width / 40, 0, width / 2, height, juce::Justification::centredLeft);
        g.setColour (palette::textMuted);
        const bool own = isUserVariant (directory, page.userRoot);
        g.drawText (own ? juce::String ("yours") : directory.getParentDirectory().getFullPathName(), width / 2 + width / 20, 0,
                    width / 2 - width / 10, height, juce::Justification::centredRight);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { page.selectVariant (row); }

private:
    ZonesPage& page;
};

ZonesPage::ZonesPage (HostView& v, std::vector<juce::File> choices, juce::File root, Dialogs d)
    : view (v), installedChoices (std::move (choices)), userRoot (std::move (root)), dialogs (std::move (d)),
      model (std::make_unique<Model> (*this)), list ("variants", model.get())
{
    if (! dialogs.confirm)
        dialogs.confirm = confirmWithBox;
    if (! dialogs.askName)
        dialogs.askName = askNameWithBox;
    look::caption (title);
    look::failure (message);
    title.setText ("ZONES", juce::dontSendNotification);
    look::help ("The spatial data per zone: decoders, layouts and zone stages. MAIN's variant also gives the files both "
                "zones share (encoder, rec, fx, phones). Installed variants and your own (~/.config/a3/engine/spatial). "
                "Layouts are edited on SPEAKERS. Save restarts the engine.",
                { &title, &mainBox, &boothBox });
    look::help ("Pick a variant to see what it holds; RENAME and DELETE (in the footer) act on it, only on your own and "
                "only on one neither what runs nor the pending edit uses.", { &list });
    const std::pair<juce::Label*, const char*> captions[] = { { &mainCaption, "MAIN" }, { &boothCaption, "BOOTH" },
                                                              { &headphonesCaption, "PHONES" } };
    for (const auto& [label, text] : captions)
    {
        look::caption (*label);
        label->setText (text, juce::dontSendNotification);
    }
    headphonesCaption.setTooltip ("The phones decoder's headphone EQ (never the rec decoder's)");
    for (auto* summary : { &mainSummary, &boothSummary })
    {
        look::value (*summary);
        summary->setJustificationType (juce::Justification::topLeft);
        summary->setMinimumHorizontalScale (1.0f);
    }
    openSpeakers.onClick = [this] { if (onOpenSpeakers) onOpenSpeakers(); };
    mainBox.onChange = [this] { chooseZone (a3::SpeakerZone::main, mainBox.getSelectedItemIndex()); };
    boothBox.onChange = [this] { chooseZone (a3::SpeakerZone::booth, boothBox.getSelectedItemIndex()); };
    headphones.addItem ("off", 1);
    int id = 2;
    for (const auto& name : a3::headphoneEqModels())
        headphones.addItem (name, id++);
    headphones.onChange = [this] { chooseHeadphoneEq (headphones.getSelectedItemIndex()); };
    for (auto* child : std::initializer_list<juce::Component*> { &title, &openSpeakers, &mainCaption, &mainBox, &boothCaption, &boothBox,
                                                                 &headphonesCaption, &headphones, &message, &list, &mainSummary, &boothSummary })
        addAndMakeVisible (child);
    selected = view.config().spatialDirectory;
    rescan();
    refresh();
}

ZonesPage::~ZonesPage() { list.setModel (nullptr); }

juce::File ZonesPage::variantAt (int row) const
{
    return row >= 0 && row < rowCount() ? directories[(size_t) row] : juce::File();
}

int ZonesPage::rowOf (const juce::File& directory) const
{
    const auto it = std::find (directories.begin(), directories.end(), directory);
    return it == directories.end() ? -1 : (int) (it - directories.begin());
}

// The installed choices, the user's variants by name, and whatever the configs name that neither holds.
void ZonesPage::rescan()
{
    std::vector<juce::File> found = installedChoices;
    const auto add = [&] (const juce::File& directory)
    {
        if (directory != juce::File() && std::find (found.begin(), found.end(), directory) == found.end())
            found.push_back (directory);
    };
    if (userRoot.isDirectory())
    {
        auto children = userRoot.findChildFiles (juce::File::findDirectories, false);
        children.sort();
        for (const auto& child : children)
            if (isUserVariant (child, userRoot) && holdsAVariant (child))
                add (child);
    }
    for (const auto& config : { view.config(), view.running() })
        for (const auto zone : { a3::SpeakerZone::main, a3::SpeakerZone::booth })
            add (zoneSpatialDirectory (config, zone));
    scannedAt = userRoot.getLastModificationTime();
    if (found == directories)
        return;
    directories = std::move (found);
    list.updateContent();
    fillZoneChoosers();
    if (rowOf (selected) < 0)
        selected = view.config().spatialDirectory;
}

void ZonesPage::fillZoneChoosers()
{
    for (auto* box : { &mainBox, &boothBox })
    {
        box->clear (juce::dontSendNotification);
        for (size_t row = 0; row < directories.size(); ++row)
            box->addItem (directories[row].getFileName() + (isUserVariant (directories[row], userRoot) ? "  (yours)" : ""), (int) row + 1);
    }
}

void ZonesPage::chooseZone (a3::SpeakerZone zone, int row)
{
    if (row < 0 || row >= rowCount())
        return;
    juce::String error;
    const auto edited = withZoneSpatialDirectory (view.config(), zone, directories[(size_t) row], error);
    look::say (message, edited ? juce::String() : error);
    if (edited && ! sameConfig (*edited, view.config()))
        view.edit (*edited);
    refresh();
}

void ZonesPage::selectVariant (int row)
{
    if (row < 0 || row >= rowCount())
        return;
    selected = directories[(size_t) row];
    list.selectRow (row, true, true);
    summarise();
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

juce::String ZonesPage::deleteRefusal() const
{
    return a3app::deleteRefusal (selected, userRoot, view.running(), view.config());
}

juce::String ZonesPage::renameRefusal() const { return a3app::renameRefusal (selected, userRoot, view.running()); }

void ZonesPage::askDelete()
{
    if (const auto why = deleteRefusal(); why.isNotEmpty())
        return look::say (message, "not deleted: " + why);
    const auto target = selected;
    dialogs.confirm ("Delete " + target.getFullPathName() + "? Its files are gone for good.",
                     [safe = juce::Component::SafePointer<ZonesPage> (this), target]
    {
        if (safe == nullptr)
            return;
        const auto deleted = deleteVariant (target, safe->userRoot, safe->view.running(), safe->view.config());
        look::say (safe->message, deleted.failed() ? "not deleted: " + deleted.getErrorMessage() : juce::String());
        safe->rescan();
        safe->refresh();
    });
}

void ZonesPage::askRename()
{
    if (const auto why = renameRefusal(); why.isNotEmpty())
        return look::say (message, "not renamed: " + why);
    const auto from = selected;
    dialogs.askName (from.getFileName(), [safe = juce::Component::SafePointer<ZonesPage> (this), from] (const juce::String& name)
    {
        if (safe == nullptr)
            return;
        juce::String error;
        const auto to = renameVariant (from, name, safe->userRoot, safe->view.running(), error);
        if (! to)
            return look::say (safe->message, "not renamed: " + error);
        look::say (safe->message, {});
        if (usesVariant (safe->view.config(), from))
            safe->view.edit (withRenamedVariant (safe->view.config(), from, *to));   // the pending edit follows
        safe->selected = *to;
        safe->rescan();
        safe->refresh();
    });
}

juce::String ZonesPage::summaryText (a3::SpeakerZone zone) const
{
    return (zone == a3::SpeakerZone::main ? mainSummary : boothSummary).getText();
}

// The selected variant and when its zone files last changed: a variant saved in place shows at once.
juce::String ZonesPage::summaryKey() const
{
    auto key = selected.getFullPathName();
    for (const auto* name : { "main.decoder.json", "main.parameters.json", "booth.decoder.json", "booth.parameters.json" })
        key << "|" << selected.getChildFile (name).getLastModificationTime().toMilliseconds();
    return key;
}

void ZonesPage::summarise()
{
    summarisedKey = summaryKey();
    look::say (mainSummary, zoneSummary (a3::SpeakerZone::main, selected));
    look::say (boothSummary, zoneSummary (a3::SpeakerZone::booth, selected));
}

void ZonesPage::refresh()
{
    if (userRoot.getLastModificationTime() != scannedAt || rowOf (zoneSpatialDirectory (view.config(), a3::SpeakerZone::main)) < 0
        || rowOf (zoneSpatialDirectory (view.config(), a3::SpeakerZone::booth)) < 0)
        rescan();
    if (summaryKey() != summarisedKey)
        summarise();
    mainBox.setSelectedItemIndex (rowOf (zoneSpatialDirectory (view.config(), a3::SpeakerZone::main)), juce::dontSendNotification);
    boothBox.setSelectedItemIndex (rowOf (zoneSpatialDirectory (view.config(), a3::SpeakerZone::booth)), juce::dontSendNotification);
    const auto choice = a3::headphoneEqChoice (view.config().engine.phonesHeadphoneEq);
    headphones.setSelectedItemIndex (choice.value_or (0), juce::dontSendNotification);
    const int row = rowOf (selected);
    if (row >= 0)
        list.selectRow (row, true, true);
    else
        list.deselectAllRows();
}

void ZonesPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

// Dropdowns first (MAIN, BOOTH, PHONES), the message, then the list over the info viewer.
void ZonesPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = look::rowHeight (*this);
    auto top = area.removeFromTop (row);
    openSpeakers.setBounds (top.removeFromRight (top.getWidth() / 3).reduced (row / 16));
    title.setBounds (top);
    const auto chooserRow = [&] (juce::Label& caption, juce::ComboBox& box)
    {
        auto line = area.removeFromTop (row).reduced (0, row / 16);
        caption.setBounds (line.removeFromLeft (line.getWidth() / 5));
        box.setBounds (line);
    };
    chooserRow (mainCaption, mainBox);
    chooserRow (boothCaption, boothBox);
    chooserRow (headphonesCaption, headphones);
    message.setBounds (area.removeFromTop (row));
    const auto summaryFont = juce::FontOptions ((float) row * 0.3f);
    for (auto* summary : { &boothSummary, &mainSummary })
    {
        summary->setFont (summaryFont);
        summary->setBounds (area.removeFromBottom (row * 2));   // five lines of the summary at its font
    }
    area.removeFromBottom (row / 4);
    list.setRowHeight (row * 3 / 4);
    list.setBounds (area);
}
}
