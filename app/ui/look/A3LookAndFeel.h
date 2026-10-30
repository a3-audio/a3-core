// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The A3 look and feel: JUCE's widgets in the A3 palette and sizes, and what JUCE has no widget for (a card, a row of the
// touch dropdown, a key of the workspace switch). Only JUCE and Tokens.h: the seed of the shared a3_look module.
#pragma once
#include "Tokens.h"

namespace a3look
{
class A3LookAndFeel : public juce::LookAndFeel_V4
{
public:
    A3LookAndFeel();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    // A card (E2), no outline: the ground every section sits on.
    virtual void drawCard (juce::Graphics&, juce::Rectangle<int> area, const layout::Tokens&);
    // The touch dropdown (Dropdown.h): the scrim over the window and one row of its list.
    virtual void drawDropdownScrim (juce::Graphics&, juce::Rectangle<int> area);
    virtual void drawDropdownRow (juce::Graphics&, const juce::String& text, int width, int height, bool selected, bool enabled);
    // The workspace switch every A3 app shares (StemDeck's keys): a key, current or not, and the list's panel.
    virtual void drawSwitchKey (juce::Graphics&, juce::Rectangle<int> area, const juce::String& label, bool current);
    virtual void drawSwitchPanel (juce::Graphics&, juce::Rectangle<float> area);
};

// The A3 look of `c` (its look and feel if it is one, else a shared default): what a component draws through.
A3LookAndFeel& lookOf (juce::Component& c);
}
