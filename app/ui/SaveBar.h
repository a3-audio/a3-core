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

// The strip under the tabs: "not saved" while the edit differs from what runs, the Save
// button and the outcome.
class SaveBar final : public juce::Component
{
public:
    explicit SaveBar (HostView&);
    ~SaveBar() override;

    void save();
    void refresh();
    const juce::Label& pendingLabel() const { return pending; }
    const juce::Label& messageLabel() const { return message; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    HostView& view;
    juce::Label pending, message;
    juce::TextButton saveButton { "Save" };
};
}
