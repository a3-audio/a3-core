// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"

namespace a3app
{
// SVF or Ladder for the channels' DJ filter. Live: the choice acts at once (FilterControls::kind),
// no Save needed; the next Save writes it into the file.
class FilterPage final : public juce::Component, public CardPage
{
public:
    explicit FilterPage (HostView&);
    ~FilterPage() override;

    void choose (a3::FilterKind);
    void refresh();
    a3::FilterKind shownKind() const;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    HostView& view;
    juce::Label title, why;   // why: what the choice is for, always visible under the keys
    juce::TextButton svfButton { juce::String::fromUTF8 ("SVF \xc2\xb7 clean, 12 dB/oct") },
                     ladderButton { juce::String::fromUTF8 ("LADDER \xc2\xb7 warm, 24 dB/oct, drives") };
};
}
