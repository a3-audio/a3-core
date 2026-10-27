// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CoreClient.h"
#include "Look.h"
#include "MonitorTable.h"

namespace a3app
{
// REGISTER: every address the system can speak, held against what Core has seen -- seen, or a dead wire --
// narrowed by device and "only dead wires". Fetched on request.
class MonitorRegisterPage final : public juce::Component
{
public:
    explicit MonitorRegisterPage (CoreClient&);
    ~MonitorRegisterPage() override;

    void refresh();
    void fetch();
    void setDevice (const std::string& device);   // empty: every device
    void setOnlyDead (bool);
    const std::vector<RegisterEntry>& entriesShown() const { return shown; }
    juce::String noteText() const { return note.getText(); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void rebuild();

    CoreClient& client;
    RegisterReport report;
    std::vector<RegisterEntry> shown;
    juce::TextButton fetchButton { "FETCH" };
    juce::ToggleButton onlyDead { "only dead wires" };
    juce::ComboBox device;
    juce::Label note;
    MonitorTable table;
};
}
