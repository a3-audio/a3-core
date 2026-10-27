// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MonitorRegisterPage.h"

#include <set>

namespace a3app
{
namespace
{
constexpr int everyDevice = 1;

}

MonitorRegisterPage::MonitorRegisterPage (CoreClient& c)
    : client (c),
      table ({ { "DIR", 0.6f }, { "ADDRESS", 3.0f }, { "DEVICE", 1.0f }, { "COUNT", 0.9f, true }, { "SOURCE", 1.2f }, { "NOTE", 2.0f } })
{
    look::caption (note);
    fetchButton.onClick = [this] { fetch(); };
    onlyDead.onClick = [this] { rebuild(); };
    device.addItem ("every device", everyDevice);
    device.setSelectedId (everyDevice, juce::dontSendNotification);
    device.onChange = [this] { rebuild(); };
    for (auto* child : std::initializer_list<juce::Component*> { &fetchButton, &onlyDead, &device, &note, &table })
        addAndMakeVisible (child);
    refresh();
}

MonitorRegisterPage::~MonitorRegisterPage() = default;

void MonitorRegisterPage::fetch() { client.requestRegister(); }

void MonitorRegisterPage::refresh()
{
    // Copies only on news: a fetch (the entries), or Core's answer (the note before a fetch).
    const auto entriesNow = client.registerGeneration(), summaryNow = client.summaryGeneration();
    if (copied > 0 && entriesNow == seen && summaryNow == seenSummary)
        return;
    seenSummary = summaryNow;
    const bool news = copied == 0 || entriesNow != seen;
    if (news)
    {
        report = client.registerReport();
        seen = entriesNow;
        ++copied;
    }
    if (! report.fetched)
    {
        look::say (note, client.summary().answered ? "not fetched yet: FETCH holds the register against what Core has seen"
                                                   : "no Core at " + client.url());
        return;
    }
    if (! news)
        return;
    const auto chosen = device.getText();
    std::set<std::string> names;
    for (const auto& entry : report.entries)
        names.insert (entry.device);
    device.clear (juce::dontSendNotification);
    device.addItem ("every device", everyDevice);
    int id = everyDevice + 1;
    for (const auto& name : names)
        device.addItem (utf8 (name), id++);
    device.setText (chosen.isEmpty() ? juce::String ("every device") : chosen, juce::dontSendNotification);
    rebuild();
}

void MonitorRegisterPage::setDevice (const std::string& name)
{
    device.setText (name.empty() ? juce::String ("every device") : utf8 (name), juce::dontSendNotification);
    rebuild();
}

void MonitorRegisterPage::setOnlyDead (bool dead)
{
    onlyDead.setToggleState (dead, juce::dontSendNotification);
    rebuild();
}

void MonitorRegisterPage::rebuild()
{
    const auto chosen = device.getSelectedId() == everyDevice ? std::string() : device.getText().toStdString();
    shown.clear();
    for (const auto& entry : report.entries)
        if ((chosen.empty() || entry.device == chosen) && (! onlyDead.getToggleState() || ! entry.seen))
            shown.push_back (entry);

    if (report.fetched)
        look::say (note, juce::String (report.seen) + " of " + juce::String ((int) report.entries.size()) + " seen, fetched at "
                             + report.fetchedAt.toString (false, true, true, true)
                             + (report.problem.empty() ? juce::String() : "; " + utf8 (report.problem)));
    table.show ((int) shown.size(),
                [this] (int row, int column) -> juce::String {
                    const auto& e = shown[(size_t) row];
                    switch (column)
                    {
                        case 0: return utf8 (e.direction);
                        case 1: return utf8 (e.address);
                        case 2: return utf8 (e.device);
                        case 3: return e.seen ? juce::String (e.count) : juce::String ("never");
                        case 4: return utf8 (e.source);
                        case 5: return utf8 (e.note);
                        default: return {};
                    }
                },
                [this] (int row, int column) {
                    if (column == 3 && ! shown[(size_t) row].seen)
                        return palette::caution;   // a dead wire: nothing has used it yet
                    return column == 1 ? palette::text : palette::textMuted;
                });
}

void MonitorRegisterPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void MonitorRegisterPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 60);
    const int row = look::rowHeight (*this);
    const int gap = row / 4;
    auto bar = area.removeFromTop (row);
    fetchButton.setBounds (bar.removeFromLeft (bar.getWidth() / 6).reduced (0, row / 10));
    bar.removeFromLeft (gap);
    onlyDead.setBounds (bar.removeFromLeft (bar.getWidth() / 4));
    device.setBounds (bar.removeFromLeft (bar.getWidth() / 3).reduced (0, row / 10));
    bar.removeFromLeft (gap);
    note.setBounds (bar);
    area.removeFromTop (gap);
    table.setBounds (area);
}
}
