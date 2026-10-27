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
    void fetch();
    const std::vector<TrafficRow>& rowsShown() const { return report.rows; }
    juce::String noteText() const { return note.getText(); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    CoreClient& client;
    UnknownReport report;
    juce::TextButton fetchButton { "FETCH" };
    juce::Label note;
    MonitorTable table;
};
}
