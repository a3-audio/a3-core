// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Dropdown.h"
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
    int copies() const { return copied; }   // how often it copied from the client (the tests')
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
    std::uint64_t seen = 0;   // the client's generation last copied
    std::uint64_t seenSummary = 0;
    int copied = 0;
    RegisterReport report;
    std::vector<RegisterEntry> shown;
    juce::TextButton fetchButton { "FETCH" };
    juce::ToggleButton onlyDead { "only dead wires" };
    Dropdown device;
    juce::Label note;
    MonitorTable table;
};
}
