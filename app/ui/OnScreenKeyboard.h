// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from StemDeck (Source/OnScreenKeyboard.h).
#pragma once
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KeyboardPolicy.h"
#include "OnboardLink.h"

namespace a3app
{
struct KeyboardTimings
{
    int hideDebounceMs = 300;   // long enough that a hop between two fields does not flash the keyboard away
    int startupPollMs = 150;    // a freshly started onboard needs a moment to claim its D-Bus name
    int startupPolls = 20;
};

// Runs KeyboardPolicy's commands against onboard, the on-screen keyboard: a text field taking focus shows
// it, focus leaving the text fields hides it a moment later, KEYS toggles it.
//
// Onboard is its own process, steered over D-Bus. The calls run on a worker thread, never on the message
// thread; when onboard does not answer it is started once and asked again. Without onboard nothing is
// started and KEYS only gives a hint.
class OnScreenKeyboard final : private juce::FocusChangeListener, private juce::Timer
{
public:
    explicit OnScreenKeyboard (std::shared_ptr<OnboardLink>, KeyboardTimings = {});
    ~OnScreenKeyboard() override;

    void keysPressed();
    // The focus moved to `focused` (the desktop tells the keyboard itself; a test calls this).
    void focusMovedTo (juce::Component* focused);
    bool isAvailable() const { return policy.isAvailable(); }

    // A short line for the window: why KEYS did nothing, or why onboard failed.
    std::function<void (const juce::String&)> onHint;

private:
    class Worker;

    void globalFocusChanged (juce::Component* focusedComponent) override { focusMovedTo (focusedComponent); }
    void timerCallback() override;
    void run (KeyboardPolicy::Command);
    void onboardFailed (const juce::String& why, bool stillAvailable);

    const int hideDebounceMs;
    KeyboardPolicy policy;
    std::unique_ptr<Worker> worker;

    JUCE_DECLARE_WEAK_REFERENCEABLE (OnScreenKeyboard)
    JUCE_DECLARE_NON_COPYABLE (OnScreenKeyboard)
};
}
