// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Look.h"
#include "HostView.h"
#include "Look.h"

namespace a3app
{
// The AUDIO page's STEREO CHANNELS AND STEMDECKS card (the CHANNELS page, then card, until 2026-10-30): N, the stereo channels, and the
// StemDecks, each a dropdown showing what is set, with the count that runs beside it. A change is a pending edit; Save
// re-creates the engine. It paints no ground: it sits on its card.
class ChannelsPage final : public juce::Component
{
public:
    explicit ChannelsPage (HostView&);
    ~ChannelsPage() override;

    void setChannels (int);
    void setDecks (int);
    void setTokens (const layout::Tokens&);   // the page's: its row, its text size
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
    layout::Tokens tokens;
    bool filling = false;
};
}
