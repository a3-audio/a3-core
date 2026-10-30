// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Look.h"
#include <functional>
#include <memory>
#include <vector>

namespace a3app
{
// A page of sub-pages, keys across the top as I/O's INPUT and OUTPUT (2026-10-30: SPEAKERS | ZONES): one sub-page
// shown at a time below the keys. Each keeps its pending edits current while hidden (its refresh runs on every tick).
class SubPages final : public juce::Component
{
public:
    explicit SubPages (const juce::String& title);
    ~SubPages() override;

    void add (const juce::String& name, std::unique_ptr<juce::Component>, std::function<void()> refresh);
    void show (const juce::String& name);
    bool has (const juce::String& name) const;
    juce::String shown() const;
    juce::Component* page (const juce::String& name) const;
    juce::TextButton* key (const juce::String& name) const;
    void refresh();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Sub
    {
        juce::String name;
        std::unique_ptr<juce::Component> page;
        std::function<void()> refresh;
    };
    std::vector<Sub> subs;
    int current = 0;
    juce::Label title;
    SubPageKeys keys { {} };
};
}
