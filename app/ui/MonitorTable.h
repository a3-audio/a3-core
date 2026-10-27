// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <optional>
#include <vector>
#include "Look.h"

namespace a3app
{
// How the monitor says an age and a rate: "0.4 s", "12 s", "3 min"; "-" before there is a rate.
juce::String ageText (double seconds);
juce::String rateText (const std::optional<double>& perSecond);
// A device's liveliness by the age of its last message (the Python page's live / stale / dead).
juce::Colour ageTone (double seconds);

// A text field in the window's look: a key's face, text off-white, the hint muted, centred in its height.
void styleField (juce::TextEditor&, const juce::String& hint);
// The field's text at a share of the height it was given (call from resized()).
void fitField (juce::TextEditor&);

// A column: its heading and its share of the table's width (shares need not add up to 1).
struct MonitorColumn
{
    juce::String name;
    float share = 1.0f;
    bool numeric = false;   // right-aligned
};

// The one table of the monitor pages: JUCE's TableListBox (recycled rows, scrolling, selection) in the
// window's look -- rows on a well, the selected row lifted, text muted unless a tone says otherwise.
// Rows are drawn from callbacks, so a page hands over what it shows and keeps no copy in the table.
class MonitorTable final : public juce::Component, private juce::TableListBoxModel
{
public:
    using Cell = std::function<juce::String (int row, int column)>;
    using Tone = std::function<juce::Colour (int row, int column)>;

    explicit MonitorTable (std::vector<MonitorColumn>);
    ~MonitorTable() override;

    // Columns count from 0. The table shows `rows` rows; a selection beyond them is dropped.
    void show (int rows, Cell, Tone = {});
    void select (int row);
    int selected() const { return list.getSelectedRow(); }
    std::function<void (int row)> onSelect;

    void resized() override;

private:
    int getNumRows() override { return rowCount; }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    std::vector<MonitorColumn> columns;
    juce::TableListBox list;
    int rowCount = 0;
    Cell cell;
    Tone tone;
};
}
