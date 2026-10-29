// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"
#include <functional>

namespace a3app
{
// Which spatial data directory (decoders, layouts, zone stages) the engine uses: a list of the
// installed variants and the user's own. Picking one is a pending edit; a directory that does not
// load is refused with the reason and the choice stays where it was. Below it what each zone plays
// (the running directory's decoder files, decoderSummary) and the key to the SPEAKERS page, where
// layouts are edited. Beside it the headphone EQ of the phones decoder (never the rec decoder's),
// also a pending edit.
class ZonesPage final : public juce::Component
{
public:
    ZonesPage (HostView&, std::vector<juce::File> choices);
    ~ZonesPage() override;

    void choose (int row);
    int rowCount() const { return (int) directories.size(); }
    int selectedRow() const;   // the row of the pending edit's directory, -1 if none
    void refresh();
    void chooseHeadphoneEq (int item);   // 0 = off, 1... = a3::headphoneEqModels()
    const juce::ComboBox& headphoneChooser() const { return headphones; }
    const juce::Label& messageLabel() const { return message; }
    juce::String summaryText (a3::SpeakerZone) const;
    std::function<void()> onOpenSpeakers;   // EDIT SPEAKERS
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Model;
    HostView& view;
    std::vector<juce::File> directories;
    void summarise();
    juce::String summaryKey() const;

    juce::Label title, message, note, headphonesCaption, mainSummary, boothSummary;
    juce::TextButton openSpeakers { "EDIT SPEAKERS" };
    juce::File summarisedFrom;
    juce::String summarisedKey;
    std::unique_ptr<Model> model;
    juce::ListBox list;
    juce::ComboBox headphones;
};
}
