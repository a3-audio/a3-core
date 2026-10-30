// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The A3 look's tokens: colours, text shares and the layout grid. Only JUCE. The seed of a shared a3_look module for
// every A3 app (the Engine window first; StemDeck, the desk, Motion UI and Canvas later): keep it app-agnostic.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace a3look
{
// One dark design language: lightness is elevation (E0 < E1 < E2 < E3), grey is structure, hue only says a state
// (green running, orange caution, red clipping/closed).
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

// A key's text as a share of the key's height: JUCE's own stops at 16 px, which reads as a quarter of a tall key.
constexpr float keyTextShare = 0.38f;
// The share of a key's width its text may take (JUCE indents a key's text on both sides).
constexpr float keyTextWidthShare = 0.8f;
// A key's text height shared with its neighbours: set on the key's properties, it caps the key's own; a row of keys
// then reads as one size, not each word as large as it fits.
inline const juce::Identifier sharedKeyTextHeight { "a3SharedKeyTextHeight" };
// A toggle's tick box and text as shares of its height.
constexpr float toggleBoxShare = 0.45f, toggleTextShare = 0.42f;

// `wanted` (a share of a height), or less so that `text` fits `width` whole.
inline float fittedTextHeight (const juce::String& text, float wanted, float width)
{
    const auto needs = juce::GlyphArrangement::getStringWidth (juce::FontOptions (wanted), text);
    return needs <= width || needs <= 0.0f ? wanted : wanted * width / needs;
}

// A page's row height as a share of the page, never below a fingertip-ish floor.
inline int rowHeight (const juce::Component& page) { return juce::jmax (28, page.getHeight() / 14); }

// The window's one layout system (2026-10-30, the maintainer: "aren't there standards for what looks symmetric and
// correct?"). Every size is a share of the page's row height (rowHeight: a fourteenth of the page, never below a
// fingertip-ish floor), so it scales with the screen; nothing is a pixel. A page is cards stacked down its margin.
namespace layout
{
struct Tokens
{
    int row = 0;        // a line: a caption and its control, a key; a control fills it but for `inset` above and below
    int unit = 0;       // the base unit, a sixth of the window's row: every spacing is a whole number of it
    int margin = 0;     // the page's edge to its cards, all four sides: 2 units
    int padding = 0;    // a card's edge to its content, all four sides: 1 unit
    int gap = 0;        // between two neighbouring cards: 1 unit
    int heading = 0;    // a card's heading line: 3/5 of a row
    int inset = 0;      // a control in its line, above and below: half a unit (two lines' controls are a unit apart)
    float radius = 0;   // a card's corner: 1 unit (no outline: lightness is elevation)
    float text = 0;     // the one text height: 0.36 of a row
};

// The page's tokens; `row` a page may set smaller to fit (AUDIO), the unit stays the page's, so margins, paddings and
// gaps are the same on every page.
inline Tokens tokens (const juce::Component& page, int row = 0)
{
    // The unit comes from the window (the top level), so every page and sub-page has the same one; the row from the page.
    const auto* window = page.getTopLevelComponent();
    Tokens t;
    t.unit = juce::jmax (4, rowHeight (window != nullptr ? *window : page) / 6);
    t.row = row > 0 ? row : rowHeight (page);
    t.margin = 2 * t.unit;
    t.padding = t.unit;
    t.gap = t.unit;
    t.heading = t.row * 3 / 5;
    t.inset = t.unit / 2;
    t.radius = (float) t.unit;
    t.text = (float) t.row * 0.36f;
    return t;
}

// A card's height for `lines` lines and an optional heading, its paddings included.
inline int cardHeight (const Tokens& t, int lines, bool heading = true)
{
    return 2 * t.padding + (heading ? t.heading : 0) + lines * t.row;
}

// A line of text with no control in it (STATUS' driver lines, ABOUT's, a program's second line): 0.55 of a row, so lines
// follow each other as text does, with no empty line between them.
constexpr float compactLineShare = 0.55f;
inline int compactLine (const Tokens& t) { return juce::roundToInt ((float) t.row * compactLineShare); }

// A line's control: the line less the inset above and below.
inline juce::Rectangle<int> control (juce::Rectangle<int> line, const Tokens& t) { return line.reduced (0, t.inset); }

// Keys side by side in `area`, equal widths, a unit apart.
inline void equalRow (juce::Rectangle<int> area, const std::vector<juce::Component*>& parts, const Tokens& t)
{
    if (parts.empty())
        return;
    const int count = (int) parts.size();
    const int width = (area.getWidth() - (count - 1) * t.unit) / count;
    for (auto* part : parts)
    {
        part->setBounds (area.removeFromLeft (width));
        area.removeFromLeft (t.unit);
    }
}

// Cards stacked down a page's area (inside its margin), a gap apart.
class Stack
{
public:
    Stack (juce::Rectangle<int> areaToFill, const Tokens& t, std::vector<juce::Rectangle<int>>& cardsMade)
        : area (areaToFill), tokens (t), cards (cardsMade) { cards.clear(); }
    juce::Rectangle<int> card (int height)
    {
        if (! cards.empty())
            area.removeFromTop (tokens.gap);
        cards.push_back (area.removeFromTop (juce::jmin (height, area.getHeight())));
        return cards.back();
    }
    juce::Rectangle<int> rest()   // a card taking what is left
    {
        return card (area.getHeight() - (cards.empty() ? 0 : tokens.gap));
    }
    int left() const { return area.getHeight() - (cards.empty() ? 0 : tokens.gap); }

private:
    juce::Rectangle<int> area;
    Tokens tokens;
    std::vector<juce::Rectangle<int>>& cards;
};
}

}
