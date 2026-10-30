// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MonitorTruthPage.h"

namespace a3app
{
namespace
{
constexpr int fingerprintShown = 12;   // as the Python page showed a hash


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

MonitorTruthPage::MonitorTruthPage (CoreClient& c, std::function<juce::String()> r)
    : client (c), retired (std::move (r)), table ({ { "DEVICE", 1.0f }, { "A3-OSC.JSON", 3.0f }, { "SAID", 0.8f, true } })
{
    look::value (fingerprint);
    look::caption (engine);
    look::caption (connection);
    look::warning (retiredLines);
    retiredLines.setJustificationType (juce::Justification::topLeft);
    for (auto* child : std::initializer_list<juce::Component*> { &fingerprint, &engine, &connection, &retiredLines, &table })
        addAndMakeVisible (child);
    refresh();
}

MonitorTruthPage::~MonitorTruthPage() = default;

void MonitorTruthPage::refresh()
{
    if (retired)
    {
        const auto text = retired();
        if (text != retiredLines.getText())
        {
            look::say (retiredLines, text);
            resized();
        }
    }
    // The summary changes once a second at most: the table is rebuilt only then.
    const auto now = client.summaryGeneration();
    if (copied > 0 && now == seen)
        return;
    summary = client.summary();
    seen = now;
    ++copied;
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
        look::say (connection, "Core at " + client.url());
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
    // The retired lines, compact, one under the other.
    const int lines = retiredLines.getText().isEmpty() ? 0 : juce::StringArray::fromLines (retiredLines.getText()).size();
    const int compact = juce::roundToInt ((float) row * layout::compactLineShare);
    retiredLines.setFont (juce::FontOptions ((float) row * 0.36f));
    retiredLines.setBounds (area.removeFromTop (lines * compact));
    area.removeFromTop (row / 4);
    table.setBounds (area);
}
}
