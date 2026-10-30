// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace a3app
{
// When the window's help tip shows, pure. JUCE's TooltipWindow is a hover model: on the touch screen the emulated
// pointer stays where the finger left, and within 500 ms of hiding a tip JUCE shows the next one at once whenever
// what lies under that resting pointer changes (a list gliding on after a drag, a row with help and one without),
// so a tip flickered and stayed. Here a tip belongs to one touch: it shows after the finger has left a control
// that has one, for a few seconds, and goes at the next touch, when its control is no longer under the pointer,
// or when its time is up. Nothing shows it again until the next release.
class TipPolicy
{
public:
    static constexpr juce::uint32 delayMs = 600, lifetimeMs = 4000;

    // A finger (or a button) went down anywhere: the tip goes, a pending one is dropped.
    void press() { pending = {}; }
    // Released over `owner` (the component whose help `tip` is; nullptr or an empty tip: none).
    void release (const void* owner, const juce::String& tip, juce::uint32 now);
    // The tip to show now ("" for none), with `under` the help owner under the pointer.
    juce::String tick (const void* under, juce::uint32 now);

private:
    struct Pending
    {
        const void* owner = nullptr;
        juce::String tip;
        juce::uint32 releasedAt = 0;
    };
    Pending pending;
};

// The help of a component: its own tooltip, else the nearest parent's (a Slider's or ComboBox's inner parts), else a
// tip a hosted plugin's control was relieved of (adoptTooltips). `owner` gets the component it came from.
juce::String helpFor (juce::Component*, juce::Component*& owner);
// Moves every tooltip below `root` into the component's properties, where only helpFor finds it: a hosted editor's
// own TooltipWindow (IEM's AllRADecoder has one) then has nothing to show, and the window's switch decides alone.
void adoptTooltips (juce::Component& root);

// The window's tip on the touch screen (TipPolicy), drawn with the look's tooltip, inside `parent` and never under
// the finger. It exists only while the window's Tooltips switch is on.
class TouchTooltips final : public juce::Component, private juce::Timer
{
public:
    explicit TouchTooltips (juce::Component& parent);
    ~TouchTooltips() override;

    // What a touch does (the global listener calls them; tests call them directly).
    void pressed();
    void released (juce::Component* at, juce::Point<int> inParent, juce::uint32 now);
    void tickAt (juce::Component* under, juce::uint32 now);
    juce::String shownTip() const { return isVisible() ? showing : juce::String(); }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    // As a global mouse listener (it takes no clicks itself): every touch of the window passes here.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::Component* componentAt (juce::Point<int> inParent) const;

    juce::Component& parent;
    TipPolicy policy;
    juce::String showing;
    juce::Point<int> anchor;   // where the finger left, in the parent
};
}
