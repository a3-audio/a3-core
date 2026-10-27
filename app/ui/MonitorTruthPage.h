// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CoreClient.h"
#include "Look.h"
#include "MonitorTable.h"

namespace a3app
{
// TRUTH & DEVICES: Core's fingerprint of a3-osc.json, each device that said hello with "is Core's" or
// "differs", and which audio engine runs.
class MonitorTruthPage final : public juce::Component
{
public:
    explicit MonitorTruthPage (CoreClient&);
    ~MonitorTruthPage() override;

    void refresh();
    int copies() const { return copied; }   // how often it copied from the client (the tests')
    juce::StringArray lines() const;   // what the page says, one line per row, for the tests

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    CoreClient& client;
    std::uint64_t seen = 0;   // the client's generation last copied
    int copied = 0;
    CoreSummary summary;
    juce::Label fingerprint, engine, connection;
    MonitorTable table;
};
}
