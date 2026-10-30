// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace a3look
{
class DropdownList;

// Every dropdown of an A3 window (the A3 look; only JUCE). JUCE's ComboBox opens a PopupMenu, which scrolls only while the pointer rests on an
// arrow at its edge and takes a drag across it as choosing: on the touch screen a long list could not be scrolled.
// This one opens its items as a ListBox (a Viewport, which JUCE scrolls by a finger drag with momentum) inside the
// window, under the box or above it, and opens on a tap (released without a drag), so a drag that starts on a box
// scrolls the page's list instead of opening it. Items, ids, selection and onChange stay the ComboBox's.
class Dropdown : public juce::ComboBox
{
public:
    Dropdown();
    ~Dropdown() override;

    void showPopup() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    DropdownList* openList() const { return list.get(); }   // nullptr while closed

private:
    friend class DropdownList;
    void closeList();

    std::unique_ptr<DropdownList> list;
};

// The open list: a scrim over the whole window (a tap on it closes the list unchanged) and the rows.
class DropdownList final : public juce::Component, private juce::ListBoxModel
{
public:
    DropdownList (Dropdown& owner, juce::Component& window);
    ~DropdownList() override;

    juce::ListBox& rows() { return list; }
    juce::Rectangle<int> rowsArea() const { return list.getBounds(); }
    void choose (int row);   // what a tap on a row does
    void dismiss();          // what a tap beside the rows does

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    Dropdown& owner;
    juce::Component& window;
    juce::ListBox list;
    juce::uint32 openedAt = 0;
};
}
