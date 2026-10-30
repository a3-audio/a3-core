// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window's dropdowns on the touch screen (2026-10-30, the maintainer: they do not scroll well by touch). JUCE's
// PopupMenu scrolls only by resting on an edge arrow and takes a drag as choosing; the list here is a ListBox, which
// JUCE scrolls by a drag (Viewport::ScrollOnDragMode) and which selects only on a release without a drag.
#include <gtest/gtest.h>
#include "Dropdown.h"
#include "PageSnapshot.h"

namespace
{
constexpr int windowWidth = 768, windowHeight = 989;

struct Rig
{
    Rig()
    {
        window.setSize (windowWidth, windowHeight);
        window.addAndMakeVisible (box);
        box.setBounds (200, 700, 400, 40);
        for (int i = 1; i <= 40; ++i)
            box.addItem ("Scarlett 18i20 USB out " + juce::String (i), i);
        box.setSelectedId (33, juce::dontSendNotification);
        box.onChange = [this] { ++changes; };
    }
    juce::Component window;
    a3look::Dropdown box;
    int changes = 0;
};

juce::MouseEvent eventOn (juce::Component& target, juce::Point<float> at, bool dragged)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &target, &target, now, dragged ? at.translated (0.0f, 60.0f) : at, now, 1, dragged);
}
}

TEST (Dropdown, OpensAScrollingListInsideTheWindow)
{
    juce::MessageManagerLock lock;
    Rig rig;
    rig.box.showPopup();
    auto* list = rig.box.openList();
    ASSERT_NE (list, nullptr);
    EXPECT_EQ (list->getParentComponent(), &rig.window) << "inside the window, not a desktop window i3 would place";
    EXPECT_TRUE (rig.window.getLocalBounds().contains (list->rowsArea()));
    auto& rows = list->rows();
    EXPECT_EQ (rows.getListBoxModel()->getNumRows(), 40);
    ASSERT_NE (rows.getViewport(), nullptr);
    EXPECT_EQ (rows.getViewport()->getScrollOnDragMode(), juce::Viewport::ScrollOnDragMode::all)
        << "both of a touch's events (touch#0 and the emulated mouse#0) scroll, neither chooses on a drag";
    EXPECT_GT (rows.getViewport()->getViewedComponent()->getHeight(), rows.getViewport()->getViewHeight()) << "40 rows scroll";
    EXPECT_GE (rows.getRowHeight(), rig.box.getHeight()) << "a row is a finger's target, at least the box's height";
    const auto selected = rows.getRowPosition (32, true);
    EXPECT_TRUE (rows.getLocalBounds().contains (selected)) << "opens on the chosen item: " << selected.toString();
    a3apptest::expectPainted (a3apptest::snapshotAt (rig.window, "dropdown-open", windowWidth, windowHeight));
}

TEST (Dropdown, ARowChoosesAndClosesATapBesideClosesUnchanged)
{
    juce::MessageManagerLock lock;
    Rig rig;
    rig.box.showPopup();
    ASSERT_NE (rig.box.openList(), nullptr);
    rig.box.openList()->choose (4);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (rig.box.getSelectedId(), 5);
    EXPECT_EQ (rig.changes, 1);
    EXPECT_EQ (rig.box.openList(), nullptr);

    rig.box.showPopup();
    ASSERT_NE (rig.box.openList(), nullptr) << "opens again";
    rig.box.openList()->dismiss();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (rig.box.openList(), nullptr);
    EXPECT_EQ (rig.box.getSelectedId(), 5);
    EXPECT_EQ (rig.changes, 1);
}

// A drag that starts on a box (a page's list scrolled by a finger) opens nothing; a tap does.
TEST (Dropdown, OpensOnATapNotOnADrag)
{
    juce::MessageManagerLock lock;
    Rig rig;
    const juce::Point<float> inside (20.0f, 20.0f);
    rig.box.mouseDown (eventOn (rig.box, inside, false));
    rig.box.mouseDrag (eventOn (rig.box, inside, true));
    rig.box.mouseUp (eventOn (rig.box, inside, true));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (rig.box.openList(), nullptr);
    rig.box.mouseDown (eventOn (rig.box, inside, false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (rig.box.openList(), nullptr) << "not yet on the touch";
    rig.box.mouseUp (eventOn (rig.box, inside, false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_NE (rig.box.openList(), nullptr);
}
