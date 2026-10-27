// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from StemDeck (Source/KeyboardPolicy.h).
#pragma once

namespace a3app
{
// When the on-screen keyboard (onboard) shows and hides. Pure: fed the events, returns what to do;
// OnScreenKeyboard runs the commands.
//
// Onboard's own auto-show follows AT-SPI accessibility, which JUCE windows do not offer, so the window
// switches it from its own focus changes.
class KeyboardPolicy
{
public:
    enum class Command
    {
        none,
        show,
        hide,
        toggle,    // KEYS: whatever onboard shows now, the other way round
        hideSoon,  // arm the debounce; hideDue() decides when it runs out
        hint       // KEYS without onboard: say why nothing happens
    };

    explicit KeyboardPolicy (bool onboardAvailable);

    // `textField`: the text field that has focus now, or nullptr. Compared only, never dereferenced.
    Command focusChanged (const void* textField);
    Command hideDue();
    Command keysPressed();

    void setAvailable (bool onboardAvailable);
    bool isAvailable() const { return available; }

private:
    bool available;
    const void* focused = nullptr;
    // The field focused when KEYS closed the keyboard: its focus coming back (onboard hands the window
    // back) does not open it again.
    const void* dismissed = nullptr;
    bool shown = false;
    bool hidePending = false;
};
}
