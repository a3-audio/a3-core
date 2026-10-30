// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"

namespace a3app
{
// CALIBRATION's FILTER card (the FILTER page until 2026-10-31): the DJ filter's kind, SVF or LADDER, live (acts at once,
// needs no Save). Its heading and the two keys; no explanation. It paints no ground: it sits on its card.
class FilterCard final : public juce::Component
{
public:
    explicit FilterCard (HostView&);
    ~FilterCard() override;

    static constexpr int lines = 1;   // under its heading
    void choose (a3::FilterKind);
    void refresh();
    a3::FilterKind shownKind() const;
    void setTokens (const layout::Tokens&);
    void resized() override;

private:
    HostView& view;
    layout::Tokens tokens;
    juce::Label title;
    juce::TextButton svfButton { "SVF" }, ladderButton { "LADDER" };
};
}
