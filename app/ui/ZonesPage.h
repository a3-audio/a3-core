// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Look.h"
#include "HostView.h"
#include "Look.h"
#include <array>
#include <functional>

namespace a3app
{
// What the info viewer shows of one zone of a variant (ZONES, 2026-10-30): the decoder's facts as label and value
// (with units), and the loudspeakers as table rows: channel (or "imaginary"), azimuth, elevation in degrees.
struct ZoneInfo
{
    juce::String zone, directory, error;   // error: the file did not read; facts and speakers are then empty
    std::vector<std::pair<juce::String, juce::String>> facts;
    std::vector<std::array<juce::String, 3>> speakers;
};
ZoneInfo zoneInfo (a3::SpeakerZone, const juce::File& variant);

// The spatial data (decoders, layouts, zone stages) per zone, top to bottom: MAIN's and BOOTH's variant and the
// phones' headphone EQ as dropdowns (each a pending edit; a variant that does not load is refused with the
// reason and the choice stays where it was); then the variants in a list (the installed ones and the user's
// own under ~/.config/a3/engine/spatial, each marked when it runs or is the pending edit; the package's default by its
// name, "Default 4.1") and an info card for the picked one (its MAIN and BOOTH decoders side by side as tables,
// zoneInfo). Picking in the list edits nothing. In the window's footer: LOAD applies the zones' choice now (written and
// applied as Save would, the other pending edits staying pending), RENAME and DELETE act only on an unused variant of
// the user's own and say why not on any other. No explanation lines (2026-10-31).
class ZonesPage final : public juce::Component, public CardPage
{
public:
    // How the page asks: a yes/no question (DELETE), and a new name (RENAME). Empty: JUCE's AlertWindow.
    struct Dialogs
    {
        std::function<void (const juce::String& question, std::function<void()> onYes)> confirm;
        std::function<void (const juce::String& current, std::function<void (const juce::String&)> onName)> askName;
    };

    // `choices`: the installed variants offered; `userRoot`: the user's variants, offered too, the only ones
    // RENAME and DELETE touch (empty: none).
    ZonesPage (HostView&, std::vector<juce::File> choices, juce::File userRoot = {}, Dialogs = {});
    ~ZonesPage() override;

    int rowCount() const { return (int) directories.size(); }
    juce::File variantAt (int row) const;
    void chooseZone (a3::SpeakerZone, int row);   // the zone's dropdown
    const juce::ComboBox& zoneChooser (a3::SpeakerZone zone) const { return zone == a3::SpeakerZone::main ? mainBox : boothBox; }
    void selectVariant (int row);                 // the list: what the info viewer shows, RENAME and DELETE act on
    juce::File selectedVariant() const { return selected; }
    void chooseHeadphoneEq (int item);            // 0 = off, 1... = a3::headphoneEqModels()
    const juce::ComboBox& headphoneChooser() const { return headphones; }

    juce::String deleteRefusal() const;           // "" when the selected variant can be deleted
    juce::String renameRefusal() const;           // "" when it can be renamed (the name is checked when given)
    void askDelete();
    void askRename();
    void loadNow();                               // LOAD

    void refresh();
    const juce::Label& messageLabel() const { return message; }
    juce::String summaryText (a3::SpeakerZone) const;   // the info card's, for the selected variant, as text
    std::function<void()> onOpenSpeakers;               // EDIT SPEAKERS
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Model;
    class Info;
    void rescan();
    void fillZoneChoosers();
    void summarise();
    juce::String summaryKey() const;
    int rowOf (const juce::File&) const;

    HostView& view;
    const std::vector<juce::File> installedChoices;
    const juce::File userRoot;
    Dialogs dialogs;
    std::vector<juce::File> directories;
    juce::Time scannedAt;   // the user root's modification time at the last scan (a rename or delete changes it)
    juce::File selected;
    juce::String summarisedKey;

    bool messageLaidOutEmpty = true;
    juce::Label title, message, mainCaption, boothCaption, headphonesCaption;
    std::unique_ptr<Info> info;
    juce::TextButton openSpeakers { "EDIT SPEAKERS" };
    Dropdown mainBox, boothBox, headphones;
    std::unique_ptr<Model> model;
    juce::ListBox list;
};
}
