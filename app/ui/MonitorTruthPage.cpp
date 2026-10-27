// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MonitorTruthPage.h"

namespace a3app
{
namespace
{
constexpr int fingerprintShown = 12;   // as the Python page showed a hash

juce::String utf8 (const std::string& text) { return juce::String::fromUTF8 (text.c_str()); }

juce::String deviceLine (const DeviceTruth& device)
{
    return device.matches ? utf8 (device.device) + ": a3-osc.json is Core's"
                          : utf8 (device.device) + ": a3-osc.json DIFFERS from Core's (" + utf8 (device.hash).substring (0, fingerprintShown) + ")";
}

juce::String engineLine (const CoreSummary& summary)
{
    return "audio engine: " + utf8 (summary.audioEngine) + (summary.reason.empty() ? juce::String() : ": " + utf8 (summary.reason));
}
}

MonitorTruthPage::MonitorTruthPage (CoreClient& c)
    : client (c), table ({ { "DEVICE", 1.0f }, { "A3-OSC.JSON", 3.0f }, { "SAID", 0.8f, true } })
{
    look::value (fingerprint);
    look::caption (engine);
    look::caption (connection);
    for (auto* child : std::initializer_list<juce::Component*> { &fingerprint, &engine, &connection, &table })
        addAndMakeVisible (child);
    refresh();
}

MonitorTruthPage::~MonitorTruthPage() = default;

void MonitorTruthPage::refresh()
{
    summary = client.summary();
    if (! summary.answered)
    {
        look::failure (connection);
        look::say (connection, "no Core at " + client.url());
        look::say (fingerprint, {});
        look::say (engine, {});
    }
    else
    {
        look::caption (connection);
        look::say (connection, "Core at " + client.url() + "; each device says which a3-osc.json it speaks when it starts");
        look::say (fingerprint, "Core's a3-osc.json: " + utf8 (summary.fingerprint).substring (0, fingerprintShown));
        look::say (engine, engineLine (summary));
        if (summary.audioEngine == "engine")
            look::caption (engine);
        else
            look::warning (engine);
    }
    table.show ((int) summary.devices.size(),
                [this] (int row, int column) -> juce::String {
                    const auto& device = summary.devices[(size_t) row];
                    switch (column)
                    {
                        case 0: return utf8 (device.device);
                        case 1: return device.matches ? juce::String ("is Core's")
                                                      : "DIFFERS (" + utf8 (device.hash).substring (0, fingerprintShown) + ")";
                        case 2: return ageText (device.age) + " ago";
                        default: return {};
                    }
                },
                [this] (int row, int column) {
                    if (column == 1)
                        return summary.devices[(size_t) row].matches ? palette::textMuted : palette::danger;
                    return column == 0 ? palette::text : palette::textMuted;
                });
}

juce::StringArray MonitorTruthPage::lines() const
{
    juce::StringArray out;
    if (! summary.answered)
        return { "no Core at " + client.url() };
    out.add (fingerprint.getText());
    out.add (engine.getText());
    for (const auto& device : summary.devices)
        out.add (deviceLine (device));
    return out;
}

void MonitorTruthPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void MonitorTruthPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 60);
    const int row = look::rowHeight (*this);
    connection.setBounds (area.removeFromTop (row));
    fingerprint.setBounds (area.removeFromTop (row));
    engine.setBounds (area.removeFromTop (row));
    area.removeFromTop (row / 4);
    table.setBounds (area);
}
}
