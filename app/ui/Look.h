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

// A key's text as a share of the key's height (the page list, FETCH, SEND, Save): JUCE's own stops at
// 16 px, which reads as a quarter of a tall key.
constexpr float keyTextShare = 0.38f;
// A toggle's tick box and text as shares of its height.
constexpr float toggleBoxShare = 0.45f, toggleTextShare = 0.42f;

// `wanted` (a share of a height), or less so that `text` fits `width` whole.
inline float fittedTextHeight (const juce::String& text, float wanted, float width)
{
    const auto needs = juce::GlyphArrangement::getStringWidth (juce::FontOptions (wanted), text);
    return needs <= width || needs <= 0.0f ? wanted : wanted * width / needs;
}

// One way to say each kind of text; the pages use these, never their own colours.
namespace look
{
inline void caption (juce::Label& label)   { label.setColour (juce::Label::textColourId, palette::textMuted); label.setJustificationType (juce::Justification::centredLeft); }
inline void value (juce::Label& label)     { label.setColour (juce::Label::textColourId, palette::text); label.setJustificationType (juce::Justification::centredLeft); label.setEditable (false); }
inline void warning (juce::Label& label)   { label.setColour (juce::Label::textColourId, palette::caution); label.setJustificationType (juce::Justification::centredLeft); }
inline void failure (juce::Label& label)   { label.setColour (juce::Label::textColourId, palette::danger); label.setJustificationType (juce::Justification::centredLeft); }
// A page's row height as a share of the page, never below a fingertip-ish floor.
inline int rowHeight (const juce::Component& page) { return juce::jmax (28, page.getHeight() / 14); }
inline void say (juce::Label& label, const juce::String& message) { label.setText (message, juce::dontSendNotification); }
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
        // Grey is structure: no accent hue on sliders and scroll bars (hue only says a state).
        setColour (juce::Slider::thumbColourId, palette::text);
        setColour (juce::Slider::trackColourId, palette::lifted);
        setColour (juce::Slider::backgroundColourId, palette::raised);
        setColour (juce::Slider::textBoxTextColourId, palette::text);
        setColour (juce::Slider::textBoxBackgroundColourId, palette::face);
        setColour (juce::Slider::textBoxOutlineColourId, palette::lifted);
        setColour (juce::ScrollBar::thumbColourId, palette::lifted);
        setColour (juce::TabbedButtonBar::tabTextColourId, palette::textMuted);
        setColour (juce::TabbedButtonBar::frontTextColourId, palette::text);
        setColour (juce::TabbedComponent::backgroundColourId, palette::surface);
        setColour (juce::TabbedComponent::outlineColourId, palette::surface);
        // The quit question: a card (E2) on the window, text off-white, keys as everywhere.
        setColour (juce::AlertWindow::backgroundColourId, palette::raised);
        setColour (juce::AlertWindow::textColourId, palette::text);
        setColour (juce::AlertWindow::outlineColourId, palette::lifted);
    }

    // The share of the key's height, never below JUCE's own (which keeps short keys as they were).
    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override
    {
        const auto juces = juce::LookAndFeel_V4::getTextButtonFont (button, buttonHeight).getHeight();
        return juce::FontOptions (juce::jmax (juces, (float) buttonHeight * keyTextShare));
    }

    // JUCE's toggle stops its text at 15 px; ours is a share of the toggle's height, as a key's is.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) override
    {
        const auto height = (float) button.getHeight();
        const auto box = height * toggleBoxShare;
        drawTickBox (g, button, height * 0.1f, (height - box) * 0.5f, box, box, button.getToggleState(), button.isEnabled(), highlighted, down);
        g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        const auto text = button.getLocalBounds().withTrimmedLeft (juce::roundToInt (height * 0.1f + box * 1.4f));
        g.setFont (juce::FontOptions (fittedTextHeight (button.getButtonText(), height * toggleTextShare, (float) text.getWidth())));
        g.drawText (button.getButtonText(), text, juce::Justification::centredLeft, false);
    }
};
}
