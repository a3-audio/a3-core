// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SubPages.h"

namespace a3app
{
SubPages::SubPages (const juce::String& text)
{
    look::caption (title);
    look::say (title, text);
    addAndMakeVisible (title);
    addAndMakeVisible (keys);
    keys.onChoose = [this] (int index) { show (subs[(size_t) index].name); };
}
SubPages::~SubPages() = default;

void SubPages::add (const juce::String& name, std::unique_ptr<juce::Component> made, std::function<void()> refreshIt)
{
    keys.add (name);
    addChildComponent (*made);
    subs.push_back ({ name, std::move (made), std::move (refreshIt) });
    show (subs.front().name);
}

void SubPages::show (const juce::String& name)
{
    for (size_t i = 0; i < subs.size(); ++i)
        if (subs[i].name == name)
            current = (int) i;
    for (size_t i = 0; i < subs.size(); ++i)
        subs[i].page->setVisible ((int) i == current);
    keys.select (current);
}

bool SubPages::has (const juce::String& name) const { return page (name) != nullptr; }

juce::String SubPages::shown() const { return subs.empty() ? juce::String() : subs[(size_t) current].name; }

juce::Component* SubPages::page (const juce::String& name) const
{
    for (const auto& sub : subs)
        if (sub.name == name)
            return sub.page.get();
    return nullptr;
}

juce::TextButton* SubPages::key (const juce::String& name) const { return keys.key (name); }

void SubPages::refresh()
{
    for (auto& sub : subs)
        if (sub.refresh)
            sub.refresh();
}

void SubPages::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

// The title and the keys in the row I/O has its INPUT and OUTPUT in (the margin, the title's quarter, equal keys a unit
// apart), the sub-page below them: its first card a gap under the keys.
void SubPages::resized()
{
    const auto t = layout::tokens (*this);
    auto top = getLocalBounds().reduced (t.margin, 0).withTrimmedTop (t.margin).withHeight (t.row);
    title.setFont (juce::FontOptions (t.text));
    title.setBounds (top.removeFromLeft (top.getWidth() / 4));
    keys.setBounds (layout::control (top, t));
    const auto below = getLocalBounds().withTrimmedTop (t.row + t.gap);   // the sub-page's own margin then sits under the keys
    for (auto& sub : subs)
        sub.page->setBounds (below);
}
}
