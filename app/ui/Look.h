// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window's pages and the A3 look (look/: the tokens, the look and feel, cards, the touch dropdown, sub-page keys;
// only JUCE, the seed of a shared a3_look module). What is here is the app's: how a page says its kinds of text.
#pragma once
#include "look/A3LookAndFeel.h"
#include "look/Card.h"
#include "look/Dropdown.h"
#include "look/SubPageKeys.h"
#include "look/Tokens.h"

namespace a3app
{
namespace palette = a3look::palette;
namespace layout = a3look::layout;
using Look = a3look::A3LookAndFeel;
using a3look::CardPage;
using a3look::Dropdown;
using a3look::DropdownList;
using a3look::SubPageKeys;
using a3look::fittedTextHeight;
using a3look::keyTextShare;
using a3look::keyTextWidthShare;
using a3look::sharedKeyTextHeight;

// One way to say each kind of text; the pages use these, never their own colours.
namespace look
{
using a3look::rowHeight;
inline void caption (juce::Label& label)   { label.setColour (juce::Label::textColourId, palette::textMuted); label.setJustificationType (juce::Justification::centredLeft); }
inline void value (juce::Label& label)     { label.setColour (juce::Label::textColourId, palette::text); label.setJustificationType (juce::Justification::centredLeft); label.setEditable (false); }
inline void warning (juce::Label& label)   { label.setColour (juce::Label::textColourId, palette::caution); label.setJustificationType (juce::Justification::centredLeft); }
inline void failure (juce::Label& label)   { label.setColour (juce::Label::textColourId, palette::danger); label.setJustificationType (juce::Justification::centredLeft); }
inline void say (juce::Label& label, const juce::String& message) { label.setText (message, juce::dontSendNotification); }
// Where a page's question opens: inside the window's content (as the quit question does), never a desktop window of
// its own, which i3 would tile beside the window (for_window [class="a3-core"]).
inline juce::Component* dialogParent (juce::Component& page)
{
    if (auto* window = page.findParentComponentOfClass<juce::ResizableWindow>())
        if (auto* content = window->getContentComponent())
            return content;
    return page.getTopLevelComponent();
}
// No tooltips (2026-10-30): a help that matters is a line on its page. A hosted plugin's controls (IEM's
// AllRADecoder brings a TooltipWindow of its own) lose theirs, so nothing pops up anywhere.
inline void stripTooltips (juce::Component& root)
{
    for (auto* child : root.getChildren())
    {
        if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (child); client != nullptr && client->getTooltip().isNotEmpty())
            client->setTooltip ({});
        stripTooltips (*child);
    }
}
}

}
