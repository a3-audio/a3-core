// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Dropdown.h"
#include "HostView.h"
#include "Look.h"

namespace a3app
{
// The AUDIO page's CHANNELS card (the CHANNELS page until 2026-10-30): N, the number of channels, and the number of
// decks, each a dropdown showing what is set, with the count that runs beside it. A change is a pending edit; Save
// re-creates the engine. It paints no ground: it sits on its card.
class ChannelsPage final : public juce::Component
{
public:
    explicit ChannelsPage (HostView&);
    ~ChannelsPage() override;

    void setChannels (int);
    void setDecks (int);
    void setTextHeight (float);
    const juce::Label& channelsRunningLabel() const { return channelsRunning; }
    const juce::Label& decksRunningLabel() const { return decksRunning; }
    juce::ComboBox& channelsChooser() { return channelsBox; }
    juce::ComboBox& decksChooser() { return decksBox; }
    void refresh();
    const juce::Label& messageLabel() const { return message; }
    void resized() override;

private:
    HostView& view;
    juce::Label caption, channelsRunning, decksCaption, decksRunning, message;
    Dropdown channelsBox, decksBox;
    float textHeight = 16.0f;
    bool filling = false;
};
}
