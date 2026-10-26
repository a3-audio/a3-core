// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"

namespace a3app
{
juce::String modeText (const AppConfig&, const BackendStatus&);   // "JACK, connecting", "Device, no links (--no-connect)"
juce::String coreText (bool heard, double secondsSince);          // "Core silent for 12.5 s"

// Read-only: engine mode, Core's last word, the gate per zone, load and xruns, missing peers
// and the per-port meters. refresh() re-reads the view (the window's timer calls it).
class StatusPage final : public juce::Component
{
public:
    explicit StatusPage (HostView&);
    ~StatusPage() override;

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Meters;
    HostView& view;
    std::unique_ptr<Meters> meters;
    AppConfig config;
    BackendStatus status;
    bool heard = false, gate = false;
    double sinceCore = 0;
    a3::GateLevels gates;
};
}
