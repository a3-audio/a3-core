// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Dropdown.h"
#include "Look.h"

namespace a3app
{
namespace
{
// JUCE's own guard (PopupMenu: windowCreationTime + 250): the touch that opened the list sends a second event (the
// emulated mouse after the touch), which must not land on the list as a choice or a dismissal.
constexpr juce::uint32 openingTouchMs = 250;
constexpr float textShare = 0.45f;   // a row's text as a share of its height, as the boxes' own
}

Dropdown::Dropdown() = default;

Dropdown::~Dropdown() { closeList(); }

void Dropdown::showPopup()
{
    auto* window = getTopLevelComponent();
    if (list != nullptr || window == nullptr || window == this || ! isEnabled())
        return;
    list = std::make_unique<DropdownList> (*this, *window);
}

void Dropdown::closeList()
{
    if (list == nullptr)
        return;
    if (auto* parent = list->getParentComponent())
        parent->removeChildComponent (list.get());
    list.reset();
    hidePopup();   // the ComboBox's own flag, so a key press can open it again
    repaint();
}

// Opens on a tap: a touch released on the box without a drag. A drag that began on the box belongs to whatever
// scrolls beneath it (the I/O page's list).
void Dropdown::mouseDown (const juce::MouseEvent&) {}
void Dropdown::mouseDrag (const juce::MouseEvent&) {}

void Dropdown::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown() || ! getLocalBounds().contains (e.getEventRelativeTo (this).getPosition()))
        return;
    showPopup();
}

DropdownList::DropdownList (Dropdown& o, juce::Component& w)
    : owner (o), window (w), openedAt (juce::Time::getMillisecondCounter())
{
    list.setModel (this);
    list.setColour (juce::ListBox::outlineColourId, palette::lifted);
    list.setOutlineThickness (1);
    list.setRowHeight (juce::jmax (owner.getHeight(), 1));
    if (auto* scroller = list.getViewport())
    {
        // Both of a touch's events scroll (JUCE 9 on X sends touch#0 and an emulated mouse#0); a row is chosen only on
        // a release without a drag (ListBox's own rule while scrolling on drag).
        scroller->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        scroller->setScrollBarsShown (true, false);
    }
    addAndMakeVisible (list);
    window.addAndMakeVisible (this);
    setAlwaysOnTop (true);
    setBounds (window.getLocalBounds());
    list.updateContent();
    const int chosen = owner.getSelectedItemIndex();
    if (chosen >= 0)
    {
        list.selectRow (chosen, true, true);
        list.scrollToEnsureRowIsOnscreen (chosen);
    }
}

DropdownList::~DropdownList() { list.setModel (nullptr); }

int DropdownList::getNumRows() { return owner.getNumItems(); }

// Under the box when the rows fit there, else above it, else wherever the most room is; the box's width.
void DropdownList::resized()
{
    const auto box = getLocalArea (&owner, owner.getLocalBounds());
    const auto margin = juce::jmax (1, getHeight() / 60);
    const auto room = getLocalBounds().reduced (margin);
    const int wanted = list.getRowHeight() * getNumRows() + 2;
    const int below = room.getBottom() - box.getBottom(), above = box.getY() - room.getY();
    const int height = juce::jmin (wanted, juce::jmax (below, above));
    const int top = (wanted <= below || below >= above) ? box.getBottom() : box.getY() - height;
    list.setBounds (juce::Rectangle<int> (box.getX(), top, box.getWidth(), height).constrainedWithin (room));
}

void DropdownList::paint (juce::Graphics& g) { g.fillAll (palette::ground.withAlpha (0.35f)); }

void DropdownList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= owner.getNumItems())
        return;
    g.fillAll (selected ? palette::lifted : palette::raised);
    g.setColour (palette::face);
    g.fillRect (0, height - 1, width, 1);
    const bool enabled = owner.isItemEnabled (owner.getItemId (row));
    g.setColour ((selected ? palette::text : palette::textMuted).withMultipliedAlpha (enabled ? 1.0f : 0.5f));
    const auto text = owner.getItemText (row);
    const auto inset = height / 3;
    g.setFont (juce::FontOptions (fittedTextHeight (text, (float) height * textShare, (float) (width - 2 * inset))));
    g.drawText (text, inset, 0, width - 2 * inset, height, juce::Justification::centredLeft, true);
}

void DropdownList::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (juce::Time::getMillisecondCounter() - openedAt >= openingTouchMs)
        choose (row);
}

void DropdownList::choose (int row)
{
    if (row < 0 || row >= owner.getNumItems() || ! owner.isItemEnabled (owner.getItemId (row)))
        return;
    const auto id = owner.getItemId (row);
    juce::Component::SafePointer<Dropdown> box (&owner);
    // The list is deleted by its own click: close it after this callback, then say the choice.
    juce::MessageManager::callAsync ([box, id]
    {
        if (box == nullptr)
            return;
        box->closeList();
        box->setSelectedId (id, juce::sendNotificationSync);
    });
}

void DropdownList::dismiss()
{
    juce::Component::SafePointer<Dropdown> box (&owner);
    juce::MessageManager::callAsync ([box] { if (box != nullptr) box->closeList(); });
}

void DropdownList::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent != this || juce::Time::getMillisecondCounter() - openedAt < openingTouchMs)
        return;
    dismiss();
}
}
