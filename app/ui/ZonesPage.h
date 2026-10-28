// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"

namespace a3app
{
// Which spatial data directory (decoders, layouts, zone stages) the engine uses: a list of the
// installed variants and the user's own, no layout editor. Picking one is a pending edit;
// a directory that does not load is refused with the reason and the choice stays where it was.
// Beside it the headphone EQ of the phones decoder (never the rec decoder's), also a pending edit.
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
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Model;
    HostView& view;
    std::vector<juce::File> directories;
    juce::Label title, message, note, headphonesCaption;
    std::unique_ptr<Model> model;
    juce::ListBox list;
    juce::ComboBox headphones;
};
}
