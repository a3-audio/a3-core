// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window's help on the touch screen (2026-10-30, the maintainer: an info "twitches" on INPUT; with Tooltips off
// nothing may pop up anywhere; with it on a tip must go by itself after a few seconds and at the next touch).
#include <gtest/gtest.h>
#include "TouchTooltips.h"

namespace
{
using a3app::TipPolicy;
constexpr juce::uint32 released = 1000;
const int ownerA = 0, ownerB = 0;
}

TEST (TipPolicy, ATipShowsAfterTheReleaseAndGoesByItself)
{
    TipPolicy policy;
    policy.release (&ownerA, "help", released);
    EXPECT_EQ (policy.tick (&ownerA, released + TipPolicy::delayMs - 1), "");
    EXPECT_EQ (policy.tick (&ownerA, released + TipPolicy::delayMs), "help");
    EXPECT_EQ (policy.tick (&ownerA, released + TipPolicy::delayMs + TipPolicy::lifetimeMs - 1), "help");
    EXPECT_EQ (policy.tick (&ownerA, released + TipPolicy::delayMs + TipPolicy::lifetimeMs), "");
    EXPECT_EQ (policy.tick (&ownerA, released + 60000), "") << "gone for good: no second showing";
}

TEST (TipPolicy, TheNextTouchTakesItAway)
{
    TipPolicy policy;
    policy.release (&ownerA, "help", released);
    EXPECT_EQ (policy.tick (&ownerA, released + TipPolicy::delayMs), "help");
    policy.press();
    EXPECT_EQ (policy.tick (&ownerA, released + TipPolicy::delayMs + 100), "");
}

// The twitch: what lies under the resting pointer changes (a list still gliding, a row without help between two
// with), and JUCE showed and hid by turns. Here the tip goes once and nothing brings it back before the next release.
TEST (TipPolicy, WhatMovesUnderTheFingerHidesItOnceAndNeverFlickers)
{
    TipPolicy policy;
    policy.release (&ownerA, "help", released);
    EXPECT_EQ (policy.tick (&ownerA, released + TipPolicy::delayMs), "help");
    EXPECT_EQ (policy.tick (nullptr, released + TipPolicy::delayMs + 100), "");
    for (juce::uint32 t = 200; t < 3000; t += 100)
        EXPECT_EQ (policy.tick (t % 200 == 0 ? &ownerA : &ownerB, released + TipPolicy::delayMs + t), "") << t;
}

TEST (TipPolicy, NothingWithoutHelp)
{
    TipPolicy policy;
    policy.release (&ownerA, "", released);
    EXPECT_EQ (policy.tick (&ownerA, released + TipPolicy::delayMs), "");
    policy.release (nullptr, "help", released);
    EXPECT_EQ (policy.tick (nullptr, released + TipPolicy::delayMs), "");
}

// A control's help is found from its inner parts (a ComboBox's label, a Slider's text box), and a hosted editor's
// tooltips are moved where only the window's tip finds them.
TEST (TouchTooltips, HelpIsFoundFromAnInnerPartAndAdoptedFromAHostedEditor)
{
    juce::MessageManagerLock lock;
    juce::Component editor;
    juce::TextButton hosted ("Noise");
    hosted.setTooltip ("Plays noise");
    editor.addAndMakeVisible (hosted);
    a3app::adoptTooltips (editor);
    EXPECT_TRUE (hosted.getTooltip().isEmpty()) << "its own TooltipWindow finds nothing";
    juce::Component* owner = nullptr;
    EXPECT_EQ (a3app::helpFor (&hosted, owner), "Plays noise");
    EXPECT_EQ (owner, &hosted);

    juce::ComboBox box;
    box.setTooltip ("Pick one");
    ASSERT_GT (box.getNumChildComponents(), 0);
    EXPECT_EQ (a3app::helpFor (box.getChildComponent (0), owner), "Pick one");
    ASSERT_NE (owner, nullptr);
    EXPECT_TRUE (owner == &box || owner->getParentComponent() == &box) << "the box or its own label (JUCE gives it the tip)";
    juce::Component plain;
    EXPECT_EQ (a3app::helpFor (&plain, owner), "");
    EXPECT_EQ (owner, nullptr);
}

TEST (TouchTooltips, ShowsInsideTheWindowAwayFromTheFingerAndGoes)
{
    juce::MessageManagerLock lock;
    juce::Component window;
    window.setSize (768, 989);
    juce::TextButton key ("MEASURE");
    key.setTooltip ("Measures the round trip now");
    window.addAndMakeVisible (key);
    key.setBounds (300, 900, 160, 50);
    a3app::TouchTooltips tips (window);
    const juce::Point<int> finger (380, 925);
    tips.released (&key, finger, released);
    tips.tickAt (&key, released + TipPolicy::delayMs);
    EXPECT_EQ (tips.shownTip(), "Measures the round trip now");
    EXPECT_TRUE (window.getLocalBounds().contains (tips.getBounds()));
    EXPECT_FALSE (tips.getBounds().contains (finger)) << "never under the finger";
    tips.pressed();
    EXPECT_EQ (tips.shownTip(), "");
}
