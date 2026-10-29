// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"
#include <functional>

namespace a3app
{
// The spatial data (decoders, layouts, zone stages) per zone, top to bottom: MAIN's and BOOTH's variant and the
// phones' headphone EQ as dropdowns (each a pending edit; a variant that does not load is refused with the
// reason and the choice stays where it was); then the variants in a list (the installed ones and the user's
// own under ~/.config/a3/engine/spatial) and an info viewer showing only the one picked in the list (its MAIN
// and BOOTH decoders, decoderSummary). Picking in the list edits nothing: it is what RENAME and DELETE (in the
// window's footer) act on, and only on an unused variant of the user's own.
class ZonesPage final : public juce::Component
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

    void refresh();
    const juce::Label& messageLabel() const { return message; }
    juce::String summaryText (a3::SpeakerZone) const;   // the info viewer's, for the selected variant
    std::function<void()> onOpenSpeakers;               // EDIT SPEAKERS
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Model;
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

    juce::Label title, message, mainCaption, boothCaption, headphonesCaption, mainSummary, boothSummary;
    juce::TextButton openSpeakers { "EDIT SPEAKERS" };
    juce::ComboBox mainBox, boothBox, headphones;
    std::unique_ptr<Model> model;
    juce::ListBox list;
};
}
