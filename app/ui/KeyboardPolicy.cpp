// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from StemDeck (Source/KeyboardPolicy.cpp).
#include "KeyboardPolicy.h"

namespace a3app
{
KeyboardPolicy::KeyboardPolicy (bool onboardAvailable) : available (onboardAvailable) {}

KeyboardPolicy::Command KeyboardPolicy::focusChanged (const void* textField)
{
    if (! available)
        return Command::none;

    const auto hadTextFocus = focused != nullptr;
    focused = textField;

    if (textField != nullptr)
    {
        hidePending = false;
        if (textField == dismissed)
            return Command::none;
        dismissed = nullptr;
        shown = true;
        return Command::show;
    }

    // Focus moving between things that were never text fields: a keyboard opened with KEYS is the hand's
    // business, not the focus's.
    if (! hadTextFocus)
        return Command::none;

    hidePending = true;
    return Command::hideSoon;
}

KeyboardPolicy::Command KeyboardPolicy::hideDue()
{
    const auto due = available && hidePending && focused == nullptr;
    hidePending = false;
    if (! due)
        return Command::none;
    // Really left: coming back to the field shows the keyboard again.
    shown = false;
    dismissed = nullptr;
    return Command::hide;
}

KeyboardPolicy::Command KeyboardPolicy::keysPressed()
{
    if (! available)
        return Command::hint;
    hidePending = false;
    shown = ! shown;
    dismissed = shown ? nullptr : focused;
    return Command::toggle;
}

void KeyboardPolicy::setAvailable (bool onboardAvailable)
{
    available = onboardAvailable;

    if (! available)
        hidePending = false;
}
}
