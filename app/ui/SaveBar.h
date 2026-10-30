// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"

namespace a3app
{
// Save: writes the pending edit to the config file (refused if the loader would refuse it),
// then applies it when it differs from what runs.
juce::Result saveEdits (HostView&);
// The same for `config` instead of the pending edit (ZONES' LOAD: the zones' choice alone, applied now).
juce::Result saveConfig (HostView&, const AppConfig& config);

// The right part of the window's footer: "not saved" while the edit differs from what runs and the
// outcome (one line each), the Save button on the right.
class SaveBar final : public juce::Component
{
public:
    explicit SaveBar (HostView&);
    ~SaveBar() override;

    void save();
    void refresh();
    const juce::Label& pendingLabel() const { return pending; }
    const juce::Label& messageLabel() const { return message; }
    const juce::Label& clockLabel() const { return clock; }   // the clock's one word, on every page
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    HostView& view;
    juce::Label pending, message, clock;
    juce::TextButton saveButton { "Save" };
};
}
