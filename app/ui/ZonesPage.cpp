// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ZonesPage.h"
#include <algorithm>

namespace a3app
{
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
    look::caption (note);
    title.setText ("ZONES", juce::dontSendNotification);
    note.setText ("The spatial data: decoders, layouts and zone stages. Installed variants and your own "
                  "(~/.config/a3/engine/spatial). Layouts are edited as files, not here. Save restarts the engine.",
                  juce::dontSendNotification);
    list.setRowHeight (juce::jmax (28, 640 / 14));
    for (auto* child : std::initializer_list<juce::Component*> { &title, &message, &note, &list })
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

void ZonesPage::refresh()
{
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
    note.setBounds (area.removeFromBottom (row * 2));
    message.setBounds (area.removeFromBottom (row));
    list.setBounds (area);
}
}
