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

juce::String crossoverText (const a3::DecoderSummary& s)
{
    if (s.lowPassHz == s.highPassHz)
        return degrees (s.lowPassHz) + " Hz";
    return "sub < " + degrees (s.lowPassHz) + " Hz, tops > " + degrees (s.highPassHz) + " Hz";
}

juce::String inDegrees (float value) { return degrees (value) + juce::String::fromUTF8 ("\xc2\xb0"); }
bool holdsAVariant (const juce::File& directory) { return directory.getChildFile ("main.decoder.json").existsAsFile(); }

// The questions open inside the window (`window`, the page's top level): as desktop windows of class a3-core, i3
// tiled them beside the window on its workspace (for_window [class="a3-core"]).
void confirmWithBox (juce::Component* window, const juce::String& question, std::function<void()> onYes)
{
    const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                             .withTitle ("Delete variant").withMessage (question).withButton ("DELETE").withButton ("Cancel")
                             .withParentComponent (window);
    juce::AlertWindow::showAsync (options, [onYes = std::move (onYes)] (int result) { if (result == 1 && onYes) onYes(); });
}

void askNameWithBox (juce::Component* window, const juce::String& current, std::function<void (const juce::String&)> onName)
{
    auto* box = new juce::AlertWindow ("Rename variant", "A new name for " + current
                                           + " (no slash, not starting with a dot, not one that exists).",
                                       juce::MessageBoxIconType::NoIcon);
    box->addTextEditor ("name", current);
    box->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
    box->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    if (window != nullptr)
    {
        window->addAndMakeVisible (box);
        box->setCentrePosition (window->getLocalBounds().getCentre());
    }
    box->enterModalState (true, juce::ModalCallbackFunction::create ([box, onName = std::move (onName)] (int result)
    {
        if (result == 1 && onName)
            onName (box->getTextEditorContents ("name"));
    }), true);   // deleted once dismissed, after this callback
}
}

ZoneInfo zoneInfo (a3::SpeakerZone zone, const juce::File& directory)
{
    const juce::String name = a3::zoneName (zone);
    ZoneInfo info { name.toUpperCase(), directory.getFullPathName(), {}, {}, {} };
    const auto decoderFile = directory.getChildFile (name + ".decoder.json");
    const auto summary = a3::decoderSummary (decoderFile.loadFileAsString(),
                                             juce::JSON::parse (directory.getChildFile (name + ".parameters.json").loadFileAsString()), info.error);
    if (! summary)
    {
        info.error = decoderFile.getFileName() + ": " + info.error;
        return info;
    }
    info.facts = { { "Decoder", summary->madeWith.isNotEmpty() ? summary->madeWith : juce::String ("not named") },
                   { "Exported", summary->exportedOn.isNotEmpty() ? summary->exportedOn : juce::String ("-") },
                   { "Order", juce::String (summary->order) },
                   { "Weights", summary->weights },
                   { "Sub", summary->subChannel > 0 ? "ch " + juce::String (summary->subChannel) : juce::String ("none") },
                   { "Crossover", crossoverText (*summary) } };
    for (const auto& s : summary->loudspeakers)
        info.speakers.push_back ({ s.imaginary ? juce::String ("imaginary") : juce::String (s.channel), inDegrees (s.azimuth), inDegrees (s.elevation) });
    return info;
}

// The picked variant's two zones side by side on a card: per zone its facts (label, value) and its loudspeakers as a
// table (channel, azimuth, elevation), every column aligned, sizes in shares of one line.
class ZonesPage::Info final : public juce::Component
{
public:
    void show (ZoneInfo mainZone, ZoneInfo boothZone)
    {
        zones = { std::move (mainZone), std::move (boothZone) };
        repaint();
    }
    const ZoneInfo& zone (a3::SpeakerZone which) const { return zones[which == a3::SpeakerZone::main ? 0 : 1]; }

    // Lines a zone's column needs: its name, the facts, the table's head and rows (or the error).
    int lines() const
    {
        int most = 0;
        for (const auto& z : zones)
            most = juce::jmax (most, z.error.isNotEmpty() ? 2 : 2 + (int) z.facts.size() + (int) z.speakers.size());
        return most;
    }

    void paint (juce::Graphics& g) override
    {
        // On its page's card (it paints no ground): two columns a unit apart either side of a hairline.
        const auto t = layout::tokens (getParentComponent() != nullptr ? *getParentComponent() : *this);   // the page's text
        auto area = getLocalBounds();
        const int line = juce::jmax (1, area.getHeight() / juce::jmax (1, lines()));
        g.setFont (juce::FontOptions (juce::jmin (t.text, (float) line * 0.72f)));
        auto left = area.removeFromLeft (area.getWidth() / 2);
        paintZone (g, zones[0], left.withTrimmedRight (t.unit), line);
        g.setColour (palette::lifted);
        g.fillRect (area.getX(), area.getY(), 1, area.getHeight());
        paintZone (g, zones[1], area.withTrimmedLeft (t.unit), line);
    }

private:
    static void paintZone (juce::Graphics& g, const ZoneInfo& z, juce::Rectangle<int> area, int line)
    {
        g.setColour (palette::text);
        g.drawText (z.zone, area.removeFromTop (line), juce::Justification::centredLeft, true);
        if (z.error.isNotEmpty())
        {
            g.setColour (palette::danger);
            g.drawFittedText (z.error, area.removeFromTop (line), juce::Justification::centredLeft, 1, 0.7f);
            return;
        }
        const int labelWidth = area.getWidth() * 2 / 5;
        for (const auto& [label, value] : z.facts)
        {
            auto row = area.removeFromTop (line);
            g.setColour (palette::textMuted);
            g.drawText (label, row.removeFromLeft (labelWidth), juce::Justification::centredLeft, true);
            g.setColour (palette::text);
            g.drawFittedText (value, row, juce::Justification::centredLeft, 1, 0.7f);
        }
        const auto columns = [&] (juce::Rectangle<int> row, const std::array<juce::String, 3>& cells)
        {
            const int width = row.getWidth() / 3;
            g.drawText (cells[0], row.removeFromLeft (width), juce::Justification::centredLeft, true);
            g.drawText (cells[1], row.removeFromLeft (width).withTrimmedRight (width / 4), juce::Justification::centredRight, true);
            g.drawText (cells[2], row.withTrimmedRight (width / 4), juce::Justification::centredRight, true);
        };
        g.setColour (palette::textMuted);
        columns (area.removeFromTop (line), { "Speaker", "Azimuth", "Elevation" });
        for (const auto& row : z.speakers)
        {
            g.setColour (row[0] == "imaginary" ? palette::textMuted : palette::text);
            columns (area.removeFromTop (line), row);
        }
    }

    std::array<ZoneInfo, 2> zones;
};


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
        juce::StringArray tags { own ? juce::String ("yours") : directory.getParentDirectory().getFullPathName() };
        if (usesVariant (page.view.running(), directory))
            tags.add ("runs");
        else if (usesVariant (page.view.config(), directory))
            tags.add ("pending");
        g.drawFittedText (tags.joinIntoString (juce::String::fromUTF8 ("  \xc2\xb7  ")), width / 2 + width / 20, 0, width / 2 - width / 10, height,
                          juce::Justification::centredRight, 1, 0.6f);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { page.selectVariant (row); }

private:
    ZonesPage& page;
};

ZonesPage::ZonesPage (HostView& v, std::vector<juce::File> choices, juce::File root, Dialogs d)
    : view (v), installedChoices (std::move (choices)), userRoot (std::move (root)), dialogs (std::move (d)),
      info (std::make_unique<Info>()), model (std::make_unique<Model> (*this)), list ("variants", model.get())
{
    if (! dialogs.confirm)
        dialogs.confirm = [this] (const juce::String& question, std::function<void()> onYes)
        { confirmWithBox (look::dialogParent (*this), question, std::move (onYes)); };
    if (! dialogs.askName)
        dialogs.askName = [this] (const juce::String& current, std::function<void (const juce::String&)> onName)
        { askNameWithBox (look::dialogParent (*this), current, std::move (onName)); };
    look::caption (title);
    look::failure (message);
    title.setText ("Spatial data", juce::dontSendNotification);
    const std::pair<juce::Label*, const char*> captions[] = { { &mainCaption, "MAIN" }, { &boothCaption, "BOOTH" },
                                                              { &headphonesCaption, "PHONES" } };
    for (const auto& [label, text] : captions)
    {
        look::caption (*label);
        label->setText (text, juce::dontSendNotification);
    }
    look::caption (actions);
    actions.setMinimumHorizontalScale (0.6f);
    openSpeakers.onClick = [this] { if (onOpenSpeakers) onOpenSpeakers(); };
    mainBox.onChange = [this] { chooseZone (a3::SpeakerZone::main, mainBox.getSelectedItemIndex()); };
    boothBox.onChange = [this] { chooseZone (a3::SpeakerZone::booth, boothBox.getSelectedItemIndex()); };
    headphones.addItem ("off", 1);
    int id = 2;
    for (const auto& name : a3::headphoneEqModels())
        headphones.addItem (name, id++);
    headphones.onChange = [this] { chooseHeadphoneEq (headphones.getSelectedItemIndex()); };
    for (auto* child : std::initializer_list<juce::Component*> { &title, &openSpeakers, &mainCaption, &mainBox, &boothCaption, &boothBox,
                                                                 &headphonesCaption, &headphones, &message, &list, &actions, info.get() })
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
    look::say (message, {});   // a refusal belonged to the variant picked before
    summarise();
    sayActions();
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
    const auto& z = info->zone (zone);
    juce::StringArray lines { z.zone, z.error };
    for (const auto& [label, value] : z.facts)
        lines.add (label + "  " + value);
    for (const auto& row : z.speakers)
        lines.add (row[0] + "  " + row[1] + "  " + row[2]);
    lines.removeEmptyStrings();
    return lines.joinIntoString ("\n");
}

// What RENAME and DELETE do for the picked variant, said before a key is pressed.
void ZonesPage::sayActions()
{
    const auto name = selected.getFileName();
    const auto renameWhy = renameRefusal(), deleteWhy = deleteRefusal();
    juce::String text;
    if (renameWhy.isEmpty() && deleteWhy.isEmpty())
        text = "RENAME and DELETE act on " + name + ".";
    else if (renameWhy == deleteWhy || ! isUserVariant (selected, userRoot))
        text = name + ": no RENAME or DELETE, " + deleteWhy + ".";
    else
        text = name + ": " + (renameWhy.isEmpty() ? juce::String ("RENAME acts on it") : "no RENAME, " + renameWhy) + "; "
               + (deleteWhy.isEmpty() ? juce::String ("DELETE acts on it") : "no DELETE, " + deleteWhy) + ".";
    look::say (actions, text);
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
    info->show (zoneInfo (a3::SpeakerZone::main, selected), zoneInfo (a3::SpeakerZone::booth, selected));
    resized();
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
    sayActions();
    if (message.getText().isEmpty() != messageLaidOutEmpty)
        resized();   // the choices' card takes the message's line only while there is one
}

void ZonesPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    paintCards (g, *this, layout::tokens (*this));
}

// Three cards (layout::Stack): the zones' choices (with EDIT SPEAKERS and the message), the variants with what RENAME
// and DELETE would do, and the picked variant's info in the rest.
void ZonesPage::resized()
{
    const auto t = layout::tokens (*this);
    for (auto* label : { &title, &mainCaption, &boothCaption, &headphonesCaption, &message, &actions })
        label->setFont (juce::FontOptions (t.text));
    layout::Stack stack (getLocalBounds().reduced (t.margin), t, cards);
    messageLaidOutEmpty = message.getText().isEmpty();
    auto area = stack.card (layout::cardHeight (t, messageLaidOutEmpty ? 4 : 5, false)).reduced (t.padding);
    auto top = area.removeFromTop (t.row);
    openSpeakers.setBounds (layout::control (top.removeFromRight (top.getWidth() / 3), t));
    title.setBounds (top);
    const auto chooserRow = [&] (juce::Label& caption, juce::ComboBox& box)
    {
        auto line = area.removeFromTop (t.row);
        caption.setBounds (line.removeFromLeft (line.getWidth() / 4));
        box.setBounds (layout::control (line, t));
    };
    chooserRow (mainCaption, mainBox);
    chooserRow (boothCaption, boothBox);
    chooserRow (headphonesCaption, headphones);
    message.setBounds (area);
    // The list as tall as its rows (scrolling past five) and the line under it.
    const int listRow = t.row * 3 / 4;
    list.setRowHeight (listRow);
    const int listHeight = listRow * juce::jlimit (1, 5, rowCount()) + 2;
    area = stack.card (2 * t.padding + listHeight + t.row).reduced (t.padding);
    list.setBounds (area.removeFromTop (listHeight));
    actions.setBounds (area);
    info->setBounds (stack.rest().reduced (t.padding));
}
}
