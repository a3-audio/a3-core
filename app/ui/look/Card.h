// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A page, or a part of one, made of cards (the A3 look): the cards it laid out, painted on its ground by the look.
#pragma once
#include "A3LookAndFeel.h"

namespace a3look
{
class CardPage
{
public:
    virtual ~CardPage() = default;
    const std::vector<juce::Rectangle<int>>& cardAreas() const { return cards; }

protected:
    void paintCards (juce::Graphics& g, juce::Component& self, const layout::Tokens& t) const
    {
        for (const auto& c : cards)
            lookOf (self).drawCard (g, c, t);
    }
    std::vector<juce::Rectangle<int>> cards;
};
}
