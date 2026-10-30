// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChannelsPage.h"

namespace a3app
{
namespace
{
void show (juce::ComboBox& box, int value)
{
    if (box.getSelectedId() != value)
        box.setSelectedId (value, juce::dontSendNotification);
}
}

ChannelsPage::ChannelsPage (HostView& v) : view (v)
{
    look::caption (caption);
    look::caption (decksCaption);
    look::caption (channelsRunning);
    look::caption (decksRunning);
    look::failure (message);
    caption.setText ("Stereo channels", juce::dontSendNotification);
    decksCaption.setText ("StemDecks", juce::dontSendNotification);
    for (int n = 1; n <= maxChannelsOnPage; ++n)
        channelsBox.addItem (juce::String (n), n);
    for (int n = 1; n <= maxDecksOnPage; ++n)
        decksBox.addItem (juce::String (n) + "   (A" + (n > 1 ? juce::String ("-") + juce::String (deckLetters (n)) : juce::String()) + ")", n);
    channelsBox.onChange = [this] { if (! filling) setChannels (channelsBox.getSelectedId()); };
    decksBox.onChange = [this] { if (! filling) setDecks (decksBox.getSelectedId()); };
    for (auto* child : std::initializer_list<juce::Component*> { &caption, &channelsBox, &channelsRunning, &decksCaption, &decksBox,
                                                                 &decksRunning, &message })
        addAndMakeVisible (child);
    refresh();
}

ChannelsPage::~ChannelsPage() = default;

void ChannelsPage::setChannels (int channels)
{
    juce::String error;
    const auto edited = withChannels (view.config(), channels, error);
    look::say (message, edited ? juce::String() : error);
    if (edited && edited->engine.channels != view.config().engine.channels)
        view.edit (*edited);
    refresh();
}

void ChannelsPage::setDecks (int decks)
{
    juce::String error;
    const auto edited = withDecks (view.config(), decks, error);
    look::say (message, edited ? juce::String() : error);
    if (edited && edited->engine.decks != view.config().engine.decks)
        view.edit (*edited);
    refresh();
}

void ChannelsPage::setTokens (const layout::Tokens& t)
{
    tokens = t;
    for (auto* label : { &caption, &channelsRunning, &decksCaption, &decksRunning, &message })
        label->setFont (juce::FontOptions (t.text));
    resized();
}

void ChannelsPage::refresh()
{
    filling = true;
    show (channelsBox, view.config().engine.channels);
    show (decksBox, view.config().engine.decks);
    filling = false;
    look::say (channelsRunning, "running: " + juce::String (view.running().engine.channels));
    look::say (decksRunning, "running: " + juce::String (view.running().engine.decks));
}

// AUDIO's rows: the caption in the first quarter, the dropdown, what runs at the right; the message below them.
void ChannelsPage::resized()
{
    auto area = getLocalBounds();
    const int row = tokens.row > 0 ? tokens.row : juce::jmax (1, area.getHeight() / 3);
    const auto line = [&] (juce::Label& label, juce::ComboBox& box, juce::Label& running)
    {
        auto r = area.removeFromTop (row);
        label.setBounds (r.removeFromLeft (r.getWidth() / 4));
        running.setBounds (r.removeFromRight (r.getWidth() / 3).withTrimmedLeft (tokens.unit));
        box.setBounds (tokens.row > 0 ? layout::control (r, tokens) : r);
    };
    line (caption, channelsBox, channelsRunning);
    line (decksCaption, decksBox, decksRunning);
    message.setBounds (area.removeFromTop (row).withTrimmedLeft (getWidth() / 4));
}
}
