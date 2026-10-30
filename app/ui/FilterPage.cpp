// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilterPage.h"
#include <limits>

namespace a3app
{
FilterPage::FilterPage (HostView& v) : view (v)
{
    look::caption (title);
    look::caption (why);
    why.setText ("The DJ filter's character on the analog channels. Decks always use SVF "
                 "(B-format: the ladder's drive would bend the 3D image).", juce::dontSendNotification);
    why.setJustificationType (juce::Justification::topLeft);
    why.setMinimumHorizontalScale (1.0f);
    title.setText ("FILTER", juce::dontSendNotification);
    svfButton.setRadioGroupId (2);
    ladderButton.setRadioGroupId (2);
    for (auto* button : { &svfButton, &ladderButton })
        button->setClickingTogglesState (false);   // the config decides which one is on
    svfButton.onClick = [this] { choose (a3::FilterKind::svf); };
    ladderButton.onClick = [this] { choose (a3::FilterKind::ladder); };
    for (auto* child : std::initializer_list<juce::Component*> { &title, &why, &svfButton, &ladderButton })
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

void FilterPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    paintCards (g, *this, layout::tokens (*this));
}

// One card (layout::Stack): the heading, the two filters as equal keys two rows tall, and why below them.
void FilterPage::resized()
{
    const auto t = layout::tokens (*this);
    layout::Stack stack (getLocalBounds().reduced (t.margin), t, cards);
    auto area = stack.card (2 * t.padding + t.heading + 2 * t.row + 2 * t.heading).reduced (t.padding);
    title.setFont (juce::FontOptions (t.text));
    title.setBounds (area.removeFromTop (t.heading));
    layout::equalRow (area.removeFromTop (2 * t.row).reduced (0, t.inset), { &svfButton, &ladderButton }, t);
    auto shared = std::numeric_limits<float>::max();   // both keys at one size: the longer text decides
    for (auto* key : { &svfButton, &ladderButton })
        shared = juce::jmin (shared, fittedTextHeight (key->getButtonText(), (float) key->getHeight() * keyTextShare * 0.7f,
                                                       (float) key->getWidth() * keyTextWidthShare));
    for (auto* key : { &svfButton, &ladderButton })
        key->getProperties().set (sharedKeyTextHeight, (double) shared);
    why.setFont (juce::FontOptions (t.text));
    why.setBounds (area);
}
}
