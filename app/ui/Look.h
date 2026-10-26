// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace a3app
{
// One dark design language for every page: lightness is elevation (E0 < E1 < E2 < E3), grey is
// structure, hue only says a state (green running, orange caution, red clipping/closed).
namespace palette
{
inline const juce::Colour ground      { 0xff0b0b0e };   // E0, behind everything
inline const juce::Colour surface     { 0xff131318 };   // E1, a page
inline const juce::Colour raised      { 0xff1b1b22 };   // E2, a card or a well
inline const juce::Colour face        { 0xff25252e };   // E3, a key's face
inline const juce::Colour lifted      { 0xff3a3a46 };   // E4, selected or pressed
inline const juce::Colour text        { 0xffe2e2e6 };   // off-white, never pure white
inline const juce::Colour textMuted   { 0xff8d8d98 };
inline const juce::Colour running     { 0xff58c27d };
inline const juce::Colour caution     { 0xffe0a24a };
inline const juce::Colour danger      { 0xffd9534f };
}

// The JUCE widgets (tabs, buttons, labels, the device selector) take their colours from here.
class Look final : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::ResizableWindow::backgroundColourId, palette::ground);
        setColour (juce::DocumentWindow::textColourId, palette::text);
        setColour (juce::Label::textColourId, palette::text);
        setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonColourId, palette::face);
        setColour (juce::TextButton::buttonOnColourId, palette::lifted);
        setColour (juce::TextButton::textColourOffId, palette::textMuted);
        setColour (juce::TextButton::textColourOnId, palette::text);
        setColour (juce::ComboBox::backgroundColourId, palette::face);
        setColour (juce::ComboBox::textColourId, palette::text);
        setColour (juce::ComboBox::outlineColourId, palette::lifted);
        setColour (juce::ComboBox::arrowColourId, palette::textMuted);
        setColour (juce::PopupMenu::backgroundColourId, palette::raised);
        setColour (juce::PopupMenu::textColourId, palette::text);
        setColour (juce::ListBox::backgroundColourId, palette::raised);
        setColour (juce::ListBox::textColourId, palette::text);
        setColour (juce::ToggleButton::textColourId, palette::text);
        setColour (juce::ToggleButton::tickColourId, palette::text);
        setColour (juce::TabbedButtonBar::tabTextColourId, palette::textMuted);
        setColour (juce::TabbedButtonBar::frontTextColourId, palette::text);
        setColour (juce::TabbedComponent::backgroundColourId, palette::surface);
        setColour (juce::TabbedComponent::outlineColourId, palette::surface);
    }
};
}
