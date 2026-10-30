// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CoreClient.h"
#include "Look.h"
#include "MonitorTable.h"

namespace a3app
{
// TRUTH & DEVICES: Core's fingerprint of a3-osc.json, each device that said hello with "is Core's" or
// "differs", which audio engine runs, and (2026-10-31, the STATUS page's DROPPED until then) what the engine's config
// file named and the load left out -- retired links and retired keys -- as compact lines.
class MonitorTruthPage final : public juce::Component
{
public:
    // `retired`: the engine's retired links and keys as lines ("" when none); empty: no such lines.
    explicit MonitorTruthPage (CoreClient&, std::function<juce::String()> retired = {});
    const juce::Label& retiredLabel() const { return retiredLines; }
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
    juce::Label fingerprint, engine, connection, retiredLines;
    std::function<juce::String()> retired;
    MonitorTable table;
};
}
