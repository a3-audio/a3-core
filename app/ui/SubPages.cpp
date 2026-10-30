// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SubPages.h"

namespace a3app
{
namespace
{
constexpr int subGroup = 4;   // the keys' radio group
}

SubPages::SubPages (const juce::String& text)
{
    look::caption (title);
    look::say (title, text);
    addAndMakeVisible (title);
}
SubPages::~SubPages() = default;

void SubPages::add (const juce::String& name, std::unique_ptr<juce::Component> made, std::function<void()> refreshIt)
{
    auto key = std::make_unique<juce::TextButton> (name);
    key->setRadioGroupId (subGroup);
    key->setClickingTogglesState (false);   // show() decides which one is on
    key->onClick = [this, name] { show (name); };
    addAndMakeVisible (*key);
    addChildComponent (*made);
    subs.push_back ({ name, std::move (made), std::move (refreshIt), std::move (key) });
    show (subs.front().name);
}

void SubPages::show (const juce::String& name)
{
    for (size_t i = 0; i < subs.size(); ++i)
        if (subs[i].name == name)
            current = (int) i;
    for (size_t i = 0; i < subs.size(); ++i)
    {
        subs[i].page->setVisible ((int) i == current);
        subs[i].key->setToggleState ((int) i == current, juce::dontSendNotification);
    }
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

juce::TextButton* SubPages::key (const juce::String& name) const
{
    for (const auto& sub : subs)
        if (sub.name == name)
            return sub.key.get();
    return nullptr;
}

void SubPages::refresh()
{
    for (auto& sub : subs)
        if (sub.refresh)
            sub.refresh();
}

void SubPages::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

// The title and the keys in the row I/O has its INPUT and OUTPUT in (its margin, the title's quarter), the sub-page
// below them.
void SubPages::resized()
{
    auto area = getLocalBounds();
    const int row = look::rowHeight (*this);
    const int margin = getWidth() / 40;
    auto top = area.removeFromTop (row + margin).reduced (margin, 0).withTrimmedTop (margin);
    title.setBounds (top.removeFromLeft (top.getWidth() / 4));
    const int width = subs.empty() ? 0 : top.getWidth() / (int) subs.size();
    for (auto& sub : subs)
        sub.key->setBounds (top.removeFromLeft (width).reduced (row / 16));
    for (auto& sub : subs)
        sub.page->setBounds (area);
}
}
