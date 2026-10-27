// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"

namespace a3app
{
// N, the number of channels (a spin box), with the DSP load beside it: the load is what N costs.
// A change is a pending edit; Save re-creates the engine.
class ChannelsPage final : public juce::Component
{
public:
    explicit ChannelsPage (HostView&);
    ~ChannelsPage() override;

    void setChannels (int);
    void refresh();
    const juce::Label& loadLabel() const { return load; }
    const juce::Label& messageLabel() const { return message; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    HostView& view;
    juce::Label title, caption, load, message, note;
    juce::Slider spin { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
};
}
