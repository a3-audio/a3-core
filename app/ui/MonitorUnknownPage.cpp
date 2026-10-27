// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MonitorUnknownPage.h"

namespace a3app
{
namespace
{
juce::String utf8 (const std::string& text) { return juce::String::fromUTF8 (text.c_str()); }
}

MonitorUnknownPage::MonitorUnknownPage (CoreClient& c)
    : client (c),
      table ({ { "ADDRESS", 3.0f }, { "PEER", 1.2f }, { "COUNT", 0.9f, true }, { "LAST VALUE", 2.0f }, { "AGE", 0.8f, true } })
{
    look::caption (note);
    fetchButton.onClick = [this] { fetch(); };
    for (auto* child : std::initializer_list<juce::Component*> { &fetchButton, &note, &table })
        addAndMakeVisible (child);
    refresh();
}

MonitorUnknownPage::~MonitorUnknownPage() = default;

void MonitorUnknownPage::fetch() { client.requestUnknown(); }

void MonitorUnknownPage::refresh()
{
    auto latest = client.unknown();
    if (! latest.fetched)
    {
        look::say (note, client.summary().answered ? "not fetched yet: FETCH asks Core once (it can be many rows)"
                                                   : "no Core at " + client.url());
        return;
    }
    if (latest.fetched && report.fetched && latest.fetchedAt == report.fetchedAt)
        return;
    report = std::move (latest);
    // The ages are as of the fetch; the note says how old the fetch is.
    look::say (note, juce::String ((int) report.rows.size()) + " addresses Core could not route, fetched at "
                         + report.fetchedAt.toString (false, true, true, true)
                         + (report.evicted > 0 ? ", " + juce::String (report.evicted) + " evicted" : juce::String()));
    table.show ((int) report.rows.size(), [this] (int row, int column) -> juce::String {
        const auto& r = report.rows[(size_t) row];
        switch (column)
        {
            case 0: return utf8 (r.address);
            case 1: return utf8 (r.peer);
            case 2: return juce::String (r.count);
            case 3: return utf8 (r.lastValue);
            case 4: return ageText (r.age);
            default: return {};
        }
    });
}

void MonitorUnknownPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void MonitorUnknownPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 60);
    const int row = look::rowHeight (*this);
    auto bar = area.removeFromTop (row);
    fetchButton.setBounds (bar.removeFromLeft (bar.getWidth() / 6).reduced (0, row / 10));
    bar.removeFromLeft (row / 4);
    note.setBounds (bar);
    area.removeFromTop (row / 4);
    table.setBounds (area);
}
}
