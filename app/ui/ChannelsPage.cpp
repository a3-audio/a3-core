// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChannelsPage.h"

namespace a3app
{
namespace
{
void makeSpin (juce::Slider& spin, int maximum, std::function<void (int)> onChange)
{
    spin.setRange (1, maximum, 1);
    spin.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
    spin.onValueChange = [&spin, onChange] { onChange ((int) spin.getValue()); };
}

void show (juce::Slider& spin, int value)
{
    if (! spin.hasKeyboardFocus (true) && (int) spin.getValue() != value)
        spin.setValue (value, juce::dontSendNotification);
}
}

ChannelsPage::ChannelsPage (HostView& v) : view (v)
{
    look::caption (title);
    look::caption (caption);
    look::value (load);
    look::caption (decksCaption);
    look::value (decksRunning);
    look::failure (message);
    title.setText ("CHANNELS", juce::dontSendNotification);
    caption.setText ("Channels (N)", juce::dontSendNotification);
    decksCaption.setText ("Decks", juce::dontSendNotification);
    look::help ("N sets the channel strips, the encoder inputs and the per-channel meter outputs. Decks sets the "
                "deck slots, 1.." + juce::String (maxDecksOnPage) + " (one letter each, A..Z); a new slot links "
                "to its local StemDeck. Save restarts the engine; Core replays its state.",
                { &title, &caption, &spin, &decksCaption, &decksSpin });
    makeSpin (spin, maxChannelsOnPage, [this] (int n) { setChannels (n); });
    makeSpin (decksSpin, maxDecksOnPage, [this] (int n) { setDecks (n); });
    for (auto* child : std::initializer_list<juce::Component*> { &title, &caption, &spin, &load, &decksCaption, &decksSpin,
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
    if (! edited)
        return;
    if (edited->engine.channels != view.config().engine.channels)
        view.edit (*edited);
}

void ChannelsPage::setDecks (int decks)
{
    juce::String error;
    const auto edited = withDecks (view.config(), decks, error);
    look::say (message, edited ? juce::String() : error);
    if (! edited)
        return;
    if (edited->engine.decks != view.config().engine.decks)
        view.edit (*edited);
}

void ChannelsPage::refresh()
{
    show (spin, view.config().engine.channels);
    show (decksSpin, view.config().engine.decks);
    look::say (load, "DSP load   " + juce::String (view.status().dspLoadPercent, 1) + " %   with N = "
                         + juce::String (view.running().engine.channels));
    look::say (decksRunning, "running: " + juce::String (view.running().engine.decks));
}

void ChannelsPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void ChannelsPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = look::rowHeight (*this);
    title.setBounds (area.removeFromTop (row));
    const auto layoutRow = [&] (juce::Label& label, juce::Slider& slider, juce::Label& value)
    {
        auto line = area.removeFromTop (row);
        label.setBounds (line.removeFromLeft (line.getWidth() / 4));
        slider.setBounds (line.removeFromLeft (line.getWidth() / 3));
        value.setBounds (line.reduced (row / 4, 0));
    };
    layoutRow (caption, spin, load);
    layoutRow (decksCaption, decksSpin, decksRunning);
    message.setBounds (area.removeFromTop (row));
}
}
