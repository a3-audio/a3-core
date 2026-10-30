// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "ClockPanel.h"
#include "Look.h"

namespace a3app
{
// Core repeats its state more often than this; silence past it means it is gone.
constexpr double coreSilentAfterSeconds = 5.0;

juce::String modeText (const AppConfig&, const BackendStatus&);   // "JACK, connecting", "Device, no links (--no-connect)"
juce::String coreText (bool heard, double secondsSince);          // "Core silent for 12.5 s"

// "client:port" names as (client, count), in the order the clients first appear.
std::vector<std::pair<std::string, int>> groupMissingByClient (const std::vector<std::string>& missingPorts);
juce::String missingText (const std::vector<std::string>& missingPorts);   // "AES67 deck B (41), system (1)" or "none"
// A count, then one line per deck slot ("deckA: cue_L cue_R"), or "none"; a line past maxLines
// becomes "+N more", so a cut is always said.
juce::String droppedText (const std::vector<std::string>& droppedLinks, int maxLines = 1000);
juce::String droppedText (const std::vector<std::string>& droppedLinks, const std::vector<std::string>& ignoredKeys,
                          int maxLines = 1000);

// What the driver runs, as STATUS says it (the AUDIO page's info lines until 2026-10-30): "44100 Hz", "Block 64 frames
// x 3 periods" (a device: no periods), where the rate comes from, the bit depth and who sets it, the DSP load, xruns.
struct DriverInfo
{
    juce::String rate, timing, rateSource, bitDepth, load, xruns;
};
DriverInfo driverInfo (const AppConfig& running, const BackendStatus&);
inline DriverInfo driverInfo (const HostView& view) { return driverInfo (view.running(), view.status()); }

// Read-only: engine mode, Core's last word, the gate per zone, load and xruns, missing peers
// and the per-port meters; the driver's details (DriverInfo) and the clock's (ClockPanel::Mode::detail). refresh() re-reads the view (the window's timer calls it).
class StatusPage final : public juce::Component
{
public:
    explicit StatusPage (HostView&);
    ~StatusPage() override;

    void refresh();
    // What runs, not the pending edit; or why no engine runs.
    const ClockPanel& clockPanel() const { return clock; }
    juce::String modeLine() const { return problem.isEmpty() ? modeText (config, status) : "NO ENGINE RUNNING - " + problem; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Meters;
    HostView& view;
    std::unique_ptr<Meters> meters;
    ClockPanel clock { ClockPanel::Mode::detail };
    AppConfig config;
    BackendStatus status;
    juce::String problem;
    std::vector<std::string> dropped, ignored;
    juce::String truthWarning;
    bool heard = false, gate = false;
    double sinceCore = 0;
    a3::GateLevels gates;
};
}
