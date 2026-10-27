// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CoreClient.h"
#include "Look.h"
#include "MonitorTable.h"

namespace a3app
{
// NOT UNDERSTOOD: what reached Core and could not be routed. Fetched on request (FETCH), never streamed:
// it can be tens of thousands of rows.
class MonitorUnknownPage final : public juce::Component
{
public:
    explicit MonitorUnknownPage (CoreClient&);
    ~MonitorUnknownPage() override;

    void refresh();
    int copies() const { return copied; }   // how often it copied from the client (the tests')
    void fetch();
    const std::vector<TrafficRow>& rowsShown() const { return report.rows; }
    juce::String noteText() const { return note.getText(); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    CoreClient& client;
    std::uint64_t seen = 0;   // the client's generation last copied
    std::uint64_t seenSummary = 0;
    int copied = 0;
    UnknownReport report;
    juce::TextButton fetchButton { "FETCH" };
    juce::Label note;
    MonitorTable table;
};
}
