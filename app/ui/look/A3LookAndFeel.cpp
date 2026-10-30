// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "A3LookAndFeel.h"

namespace a3look
{
namespace
{
// StemDeck's Theme.h, as A3 Motion's switcherLook and the desk have it: the switch looks the same in every app.
const juce::Colour switchFace { 0xff26282c }, switchCurrent { 0xff58595c }, switchOutline { 0xff34363b }, switchText { 0xffdcdcdc },
    switchCurrentText { 0xff000000 }, switchPanelFace { 0xff1c1d20 };
constexpr float switchCorner = 6.f, switchOutlineWidth = 1.f, halfPixel = 0.5f;
constexpr float dropdownTextShare = 0.45f;
}

A3LookAndFeel::A3LookAndFeel()
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
    setColour (juce::ListBox::outlineColourId, palette::lifted);
    setColour (juce::ToggleButton::textColourId, palette::text);
    setColour (juce::ToggleButton::tickColourId, palette::text);
    // Grey is structure: no accent hue on sliders and scroll bars (hue only says a state).
    setColour (juce::Slider::thumbColourId, palette::text);
    setColour (juce::Slider::trackColourId, palette::lifted);
    setColour (juce::Slider::backgroundColourId, palette::face);
    setColour (juce::Slider::textBoxTextColourId, palette::text);
    setColour (juce::Slider::textBoxBackgroundColourId, palette::face);
    setColour (juce::Slider::textBoxOutlineColourId, palette::lifted);
    setColour (juce::ScrollBar::thumbColourId, palette::lifted);
    setColour (juce::TabbedButtonBar::tabTextColourId, palette::textMuted);
    setColour (juce::TabbedButtonBar::frontTextColourId, palette::text);
    setColour (juce::TabbedComponent::backgroundColourId, palette::surface);
    setColour (juce::TabbedComponent::outlineColourId, palette::surface);
    setColour (juce::TextEditor::backgroundColourId, palette::face);
    setColour (juce::TextEditor::textColourId, palette::text);
    setColour (juce::TextEditor::outlineColourId, palette::lifted);
    // A question: a card (E2) on the window, text off-white, keys as everywhere.
    setColour (juce::AlertWindow::backgroundColourId, palette::raised);
    setColour (juce::AlertWindow::textColourId, palette::text);
    setColour (juce::AlertWindow::outlineColourId, palette::lifted);
}

// The share of the key's height, never below JUCE's own (which keeps short keys as they were); smaller when the word
// would not fit the key's width whole (a row of narrow keys).
juce::Font A3LookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const auto juces = juce::LookAndFeel_V4::getTextButtonFont (button, buttonHeight).getHeight();
    auto wanted = juce::jmax (juces, (float) buttonHeight * keyTextShare);
    if (const auto* shared = button.getProperties().getVarPointer (sharedKeyTextHeight))
        wanted = juce::jmin (wanted, (float) static_cast<double> (*shared));
    if (button.getWidth() <= 0)
        return juce::FontOptions (wanted);
    return juce::FontOptions (fittedTextHeight (button.getButtonText(), wanted, (float) button.getWidth() * keyTextWidthShare));
}

// JUCE's toggle stops its text at 15 px; ours is a share of the toggle's height, as a key's is.
void A3LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    const auto height = (float) button.getHeight();
    const auto box = height * toggleBoxShare;
    drawTickBox (g, button, height * 0.1f, (height - box) * 0.5f, box, box, button.getToggleState(), button.isEnabled(), highlighted, down);
    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    const auto text = button.getLocalBounds().withTrimmedLeft (juce::roundToInt (height * 0.1f + box * 1.4f));
    g.setFont (juce::FontOptions (fittedTextHeight (button.getButtonText(), height * toggleTextShare, (float) text.getWidth())));
    g.drawText (button.getButtonText(), text, juce::Justification::centredLeft, false);
}

void A3LookAndFeel::drawCard (juce::Graphics& g, juce::Rectangle<int> area, const layout::Tokens& t)
{
    if (area.isEmpty())
        return;
    g.setColour (palette::raised);
    g.fillRoundedRectangle (area.toFloat(), t.radius);
}

void A3LookAndFeel::drawDropdownScrim (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (palette::ground.withAlpha (0.35f));
    g.fillRect (area);
}

void A3LookAndFeel::drawDropdownRow (juce::Graphics& g, const juce::String& text, int width, int height, bool selected, bool enabled)
{
    g.fillAll (selected ? palette::lifted : palette::raised);
    g.setColour (palette::face);
    g.fillRect (0, height - 1, width, 1);
    g.setColour ((selected ? palette::text : palette::textMuted).withMultipliedAlpha (enabled ? 1.0f : 0.5f));
    const auto inset = height / 3;
    g.setFont (juce::FontOptions (fittedTextHeight (text, (float) height * dropdownTextShare, (float) (width - 2 * inset))));
    g.drawText (text, inset, 0, width - 2 * inset, height, juce::Justification::centredLeft, true);
}

// A3 Motion's switcherLook::paintKey: LookAndFeel_V4's button, which is what StemDeck's keys are.
void A3LookAndFeel::drawSwitchKey (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label, bool current)
{
    const auto face = area.toFloat().reduced (halfPixel);
    g.setColour (current ? switchCurrent : switchFace);
    g.fillRoundedRectangle (face, switchCorner);
    g.setColour (switchOutline);
    g.drawRoundedRectangle (face, switchCorner, switchOutlineWidth);
    const auto font = juce::Font (juce::FontOptions (juce::jmin (16.f, static_cast<float> (area.getHeight()) * 0.6f)));
    const auto yIndent = juce::jmin (4, juce::roundToInt (static_cast<float> (area.getHeight()) * 0.3f));
    const auto indent = juce::jmin (juce::roundToInt (font.getHeight() * 0.6f), 2 + juce::jmin (area.getWidth(), area.getHeight()) / 4);
    g.setFont (font);
    g.setColour (current ? switchCurrentText : switchText);
    g.drawFittedText (label, area.reduced (indent, yIndent), juce::Justification::centred, 2);
}

void A3LookAndFeel::drawSwitchPanel (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (switchPanelFace);
    g.fillRoundedRectangle (area, switchCorner);
    g.setColour (switchOutline);
    g.drawRoundedRectangle (area, switchCorner, switchOutlineWidth);
}

A3LookAndFeel& lookOf (juce::Component& c)
{
    if (auto* own = dynamic_cast<A3LookAndFeel*> (&c.getLookAndFeel()))
        return *own;
    static A3LookAndFeel fallback;
    return fallback;
}
}
