// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilterPage.h"

namespace a3app
{
FilterPage::FilterPage (HostView& v) : view (v)
{
    look::caption (title);
    look::caption (note);
    title.setText ("FILTER", juce::dontSendNotification);
    note.setText ("Live: the choice acts at once and needs no Save (the next Save keeps it in the file). "
                  "The decks always use the SVF.", juce::dontSendNotification);
    svfButton.setRadioGroupId (2);
    ladderButton.setRadioGroupId (2);
    for (auto* button : { &svfButton, &ladderButton })
        button->setClickingTogglesState (false);   // the config decides which one is on
    svfButton.onClick = [this] { choose (a3::FilterKind::svf); };
    ladderButton.onClick = [this] { choose (a3::FilterKind::ladder); };
    for (auto* child : std::initializer_list<juce::Component*> { &title, &note, &svfButton, &ladderButton })
        addAndMakeVisible (child);
    refresh();
}

FilterPage::~FilterPage() = default;

void FilterPage::choose (a3::FilterKind kind)
{
    if (kind != view.config().engine.filterKind)
        view.setFilterKind (kind);
    refresh();
}

a3::FilterKind FilterPage::shownKind() const { return ladderButton.getToggleState() ? a3::FilterKind::ladder : a3::FilterKind::svf; }

void FilterPage::refresh()
{
    const bool ladder = view.config().engine.filterKind == a3::FilterKind::ladder;
    svfButton.setToggleState (! ladder, juce::dontSendNotification);
    ladderButton.setToggleState (ladder, juce::dontSendNotification);
}

void FilterPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void FilterPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = look::rowHeight (*this);
    title.setBounds (area.removeFromTop (row));
    auto keys = area.removeFromTop (row * 2);
    svfButton.setBounds (keys.removeFromLeft (keys.getWidth() / 4).reduced (2));
    ladderButton.setBounds (keys.removeFromLeft (keys.getWidth() / 3).reduced (2));
    note.setBounds (area.removeFromTop (row * 2));
}
}
