// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChannelsPage.h"

namespace a3app
{
ChannelsPage::ChannelsPage (HostView& v) : view (v)
{
    look::caption (title);
    look::caption (caption);
    look::value (load);
    look::failure (message);
    look::caption (note);
    title.setText ("CHANNELS", juce::dontSendNotification);
    caption.setText ("Channels (N)", juce::dontSendNotification);
    note.setText ("N sets the channel strips, the encoder inputs and the per-channel meter outputs. "
                  "Save restarts the engine; Core replays its state.", juce::dontSendNotification);
    spin.setRange (1, maxChannelsOnPage, 1);
    spin.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
    spin.onValueChange = [this] { setChannels ((int) spin.getValue()); };
    for (auto* child : std::initializer_list<juce::Component*> { &title, &caption, &spin, &load, &message, &note })
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

void ChannelsPage::refresh()
{
    const int n = view.config().engine.channels;
    if (! spin.hasKeyboardFocus (true) && (int) spin.getValue() != n)
        spin.setValue (n, juce::dontSendNotification);
    look::say (load, "DSP load   " + juce::String (view.status().dspLoadPercent, 1) + " %   with N = "
                         + juce::String (view.running().engine.channels));
}

void ChannelsPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void ChannelsPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = look::rowHeight (*this);
    title.setBounds (area.removeFromTop (row));
    auto line = area.removeFromTop (row);
    caption.setBounds (line.removeFromLeft (line.getWidth() / 4));
    spin.setBounds (line.removeFromLeft (line.getWidth() / 3));
    load.setBounds (line.reduced (row / 4, 0));
    message.setBounds (area.removeFromTop (row));
    note.setBounds (area.removeFromTop (row * 2));
}
}
