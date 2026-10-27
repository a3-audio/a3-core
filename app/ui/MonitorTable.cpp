// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MonitorTable.h"

#include "CoreClient.h"

namespace a3app
{
void styleField (juce::TextEditor& field, const juce::String& hint)
{
    field.setTextToShowWhenEmpty (hint, palette::textMuted);
    field.setColour (juce::TextEditor::backgroundColourId, palette::face);
    field.setColour (juce::TextEditor::textColourId, palette::text);
    field.setColour (juce::TextEditor::outlineColourId, palette::lifted);
    field.setJustification (juce::Justification::centredLeft);
}

void fitField (juce::TextEditor& field)
{
    field.applyFontToAllText (juce::FontOptions ((float) field.getHeight() * 0.5f));
    field.setIndents (field.getHeight() / 4, 0);
}

juce::String ageText (double seconds)
{
    if (seconds < 10.0)
        return juce::String (juce::jmax (0.0, seconds), 1) + " s";
    if (seconds < 120.0)
        return juce::String (juce::roundToInt (seconds)) + " s";
    return juce::String (juce::roundToInt (seconds / 60.0)) + " min";
}

juce::String rateText (const std::optional<double>& perSecond) { return perSecond ? juce::String (*perSecond, 1) : juce::String ("-"); }

juce::Colour ageTone (double seconds)
{
    if (seconds < staleAfterSeconds)
        return palette::running;
    return seconds < deadAfterSeconds ? palette::caution : palette::textMuted;
}

MonitorTable::MonitorTable (std::vector<MonitorColumn> columnsToShow) : columns (std::move (columnsToShow))
{
    list.setModel (this);
    list.setColour (juce::ListBox::backgroundColourId, palette::raised);
    list.setColour (juce::ListBox::outlineColourId, palette::raised);
    auto& header = list.getHeader();
    header.setColour (juce::TableHeaderComponent::backgroundColourId, palette::surface);
    header.setColour (juce::TableHeaderComponent::textColourId, palette::textMuted);
    header.setColour (juce::TableHeaderComponent::outlineColourId, palette::surface);
    for (size_t i = 0; i < columns.size(); ++i)
        header.addColumn (columns[i].name, (int) i + 1, 100, 30, -1, juce::TableHeaderComponent::notSortable);
    addAndMakeVisible (list);
}

MonitorTable::~MonitorTable() { list.setModel (nullptr); }

void MonitorTable::show (int rows, Cell cellToUse, Tone toneToUse)
{
    rowCount = rows;
    cell = std::move (cellToUse);
    tone = std::move (toneToUse);
    if (list.getSelectedRow() >= rows)
        list.deselectAllRows();
    list.updateContent();
    list.repaint();
}

void MonitorTable::select (int row)
{
    if (row < 0 || row >= rowCount)
        list.deselectAllRows();
    else
        list.selectRow (row);
}

void MonitorTable::resized()
{
    list.setBounds (getLocalBounds());
    // A row is a share of the page the table sits on, never below what a line of text needs.
    list.setRowHeight (juce::jmax (20, getParentHeight() / 26));
    list.setHeaderHeight (list.getRowHeight());
    float total = 0;
    for (const auto& column : columns)
        total += column.share;
    const int width = getWidth() - list.getVerticalScrollBar().getWidth();
    for (size_t i = 0; i < columns.size(); ++i)
        list.getHeader().setColumnWidth ((int) i + 1, juce::jmax (30, juce::roundToInt (width * columns[i].share / total)));
}

void MonitorTable::paintRowBackground (juce::Graphics& g, int, int, int, bool selected)
{
    g.fillAll (selected ? palette::lifted : palette::raised);
}

void MonitorTable::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool selected)
{
    if (! cell || row >= rowCount)
        return;
    const int index = column - 1;
    auto colour = tone ? tone (row, index) : palette::textMuted;
    if (selected && colour == palette::textMuted)
        colour = palette::text;
    g.setColour (colour);
    g.setFont (juce::FontOptions ((float) height * 0.62f));
    const bool numeric = index >= 0 && index < (int) columns.size() && columns[(size_t) index].numeric;
    g.drawText (cell (row, index), juce::Rectangle<int> (0, 0, width, height).reduced (height / 4, 0),
                numeric ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
}

void MonitorTable::selectedRowsChanged (int lastRowSelected)
{
    if (onSelect)
        onSelect (lastRowSelected);
}
}
