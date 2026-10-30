// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilterCard.h"

namespace a3app
{
FilterCard::FilterCard (HostView& v) : view (v)
{
    look::caption (title);
    title.setText ("FILTER", juce::dontSendNotification);
    svfButton.setRadioGroupId (2);
    ladderButton.setRadioGroupId (2);
    for (auto* button : { &svfButton, &ladderButton })
        button->setClickingTogglesState (false);   // the config decides which one is on
    svfButton.onClick = [this] { choose (a3::FilterKind::svf); };
    ladderButton.onClick = [this] { choose (a3::FilterKind::ladder); };
    for (auto* child : std::initializer_list<juce::Component*> { &title, &svfButton, &ladderButton })
        addAndMakeVisible (child);
    refresh();
}

FilterCard::~FilterCard() = default;

void FilterCard::choose (a3::FilterKind kind)
{
    if (kind != view.config().engine.filterKind)
        view.setFilterKind (kind);
    refresh();
}

a3::FilterKind FilterCard::shownKind() const { return ladderButton.getToggleState() ? a3::FilterKind::ladder : a3::FilterKind::svf; }

void FilterCard::refresh()
{
    const bool ladder = view.config().engine.filterKind == a3::FilterKind::ladder;
    svfButton.setToggleState (! ladder, juce::dontSendNotification);
    ladderButton.setToggleState (ladder, juce::dontSendNotification);
}

void FilterCard::setTokens (const layout::Tokens& t)
{
    tokens = t;
    resized();
}

// The heading, then the two keys side by side in one line.
void FilterCard::resized()
{
    if (tokens.row <= 0)
        return;
    auto area = getLocalBounds();
    title.setFont (juce::FontOptions (tokens.text));
    title.setBounds (area.removeFromTop (tokens.heading));
    layout::equalRow (layout::control (area.removeFromTop (tokens.row), tokens), { &svfButton, &ladderButton }, tokens);
}
}
