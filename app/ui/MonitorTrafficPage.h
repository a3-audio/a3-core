// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Dropdown.h"
#include "CoreClient.h"
#include "Look.h"
#include "MonitorTable.h"

namespace a3app
{
// TRAFFIC: what Core understood, one row per direction and address (direction, address, peer, count,
// rate, last value, age), narrowed by a piece of the address, "only active" and one device; the selected
// row's history below. refresh() re-reads the client's cache (the window's timer calls it).
class MonitorTrafficPage final : public juce::Component
{
public:
    explicit MonitorTrafficPage (CoreClient&);
    ~MonitorTrafficPage() override;

    void refresh();
    int copies() const { return copied; }   // how often it copied from the client (the tests')
    void setFilter (const TrafficFilter&);
    TrafficFilter filter() const;
    const std::vector<TrafficRow>& rowsShown() const { return shown; }
    void selectRow (int row);
    std::vector<HistoryEntry> historyShown() const;
    juce::String statusText() const { return status.getText(); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void filterChanged();
    void showHistory();
    void copyFromClient();
    void rebuild();

    CoreClient& client;
    std::uint64_t seen = 0;   // the client's generation last copied
    int copied = 0;
    TrafficGlance report;
    TrafficFilter current;
    std::vector<std::string> peers;
    bool rebuilding = false;
    std::vector<TrafficRow> shown;
    std::string pickedDirection, pickedAddress;
    juce::TextEditor search;
    juce::ToggleButton onlyActive { "only active" };
    Dropdown device;
    juce::Label status, historyTitle;
    MonitorTable table, historyTable;
};
}
