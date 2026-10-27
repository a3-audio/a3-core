// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from StemDeck (tests/KeyboardPolicy.cpp): when the on-screen keyboard shows and hides.
#include <gtest/gtest.h>
#include "KeyboardPolicy.h"

using Command = a3app::KeyboardPolicy::Command;
using a3app::KeyboardPolicy;

// Stand-ins for two text fields; the policy only compares them.
static int searchField, nameField;
static const void* const search = &searchField;
static const void* const name = &nameField;

TEST (KeyboardPolicy, ATextFieldTakingFocusShowsTheKeyboard)
{
    KeyboardPolicy policy { true };
    EXPECT_EQ (policy.focusChanged (search), Command::show);
}

TEST (KeyboardPolicy, FocusLeavingTheTextFieldsHidesOnlyAfterAPause)
{
    KeyboardPolicy policy { true };
    policy.focusChanged (search);
    EXPECT_EQ (policy.focusChanged (nullptr), Command::hideSoon);
    EXPECT_EQ (policy.hideDue(), Command::hide);
}

// A hop from one text field to the next must not flash the keyboard away.
TEST (KeyboardPolicy, AHopBetweenTextFieldsKeepsTheKeyboard)
{
    KeyboardPolicy policy { true };
    policy.focusChanged (search);
    policy.focusChanged (nullptr);
    EXPECT_EQ (policy.focusChanged (name), Command::show);
    EXPECT_EQ (policy.hideDue(), Command::none);
}

TEST (KeyboardPolicy, AHideIsDueOnlyOnce)
{
    KeyboardPolicy policy { true };
    policy.focusChanged (search);
    policy.focusChanged (nullptr);
    policy.hideDue();
    EXPECT_EQ (policy.hideDue(), Command::none);
}

// Focus wandering between buttons never had a text field: a keyboard opened
// with KEYS stays.
TEST (KeyboardPolicy, FocusMovingOutsideTextFieldsLeavesTheKeyboardAlone)
{
    KeyboardPolicy policy { true };
    EXPECT_EQ (policy.focusChanged (nullptr), Command::none);
    EXPECT_EQ (policy.hideDue(), Command::none);
}

TEST (KeyboardPolicy, KeysToggles)
{
    KeyboardPolicy policy { true };
    EXPECT_EQ (policy.keysPressed(), Command::toggle);
    EXPECT_EQ (policy.keysPressed(), Command::toggle);
}

// KEYS pressed while a hide is pending: the hand decided, the timer does not
// undo it a moment later.
TEST (KeyboardPolicy, KeysCancelsAPendingHide)
{
    KeyboardPolicy policy { true };
    policy.focusChanged (search);
    policy.focusChanged (nullptr);
    EXPECT_EQ (policy.keysPressed(), Command::toggle);
    EXPECT_EQ (policy.hideDue(), Command::none);
}

TEST (KeyboardPolicy, WithoutOnboardKeysOnlyGivesAHint)
{
    KeyboardPolicy policy { false };
    EXPECT_EQ (policy.keysPressed(), Command::hint);
    EXPECT_EQ (policy.focusChanged (search), Command::none);
    EXPECT_EQ (policy.focusChanged (nullptr), Command::none);
    EXPECT_EQ (policy.hideDue(), Command::none);
}

TEST (KeyboardPolicy, OnboardFailingToStartMakesItUnavailable)
{
    KeyboardPolicy policy { true };
    policy.focusChanged (search);
    policy.focusChanged (nullptr);
    policy.setAvailable (false);
    EXPECT_FALSE (policy.isAvailable());
    EXPECT_EQ (policy.hideDue(), Command::none);
    EXPECT_EQ (policy.keysPressed(), Command::hint);
}

// KEYS closing the keyboard over a focused text field (2026-10-06): onboard
// disappearing hands the window back, the same field gets the focus again --
// that used to show the keyboard at once, so KEYS never closed it.
TEST (KeyboardPolicy, KeysClosingOverAFieldKeepsItClosedWhenTheFieldComesBack)
{
    KeyboardPolicy policy { true };
    EXPECT_EQ (policy.focusChanged (search), Command::show);
    EXPECT_EQ (policy.keysPressed(), Command::toggle);   // closes
    EXPECT_EQ (policy.focusChanged (nullptr), Command::hideSoon);
    EXPECT_EQ (policy.focusChanged (search), Command::none);
    EXPECT_EQ (policy.focusChanged (search), Command::none);
}

TEST (KeyboardPolicy, AfterKeysClosedItAnotherFieldShowsItAgain)
{
    KeyboardPolicy policy { true };
    policy.focusChanged (search);
    policy.keysPressed();
    EXPECT_EQ (policy.focusChanged (name), Command::show);
}

TEST (KeyboardPolicy, AfterKeysClosedItReallyLeavingAndComingBackShowsItAgain)
{
    KeyboardPolicy policy { true };
    policy.focusChanged (search);
    policy.keysPressed();
    policy.focusChanged (nullptr);
    policy.hideDue();                                     // the pause ran out
    EXPECT_EQ (policy.focusChanged (search), Command::show);
}

TEST (KeyboardPolicy, KeysOpeningItAgainOverTheFieldKeepsTheFieldsKeyboard)
{
    KeyboardPolicy policy { true };
    policy.focusChanged (search);
    policy.keysPressed();                                 // closes
    policy.keysPressed();                                 // opens again
    EXPECT_EQ (policy.focusChanged (search), Command::show);
}
