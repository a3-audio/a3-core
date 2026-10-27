// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MonitorTrafficPage.h"

#include <algorithm>

namespace a3app
{
namespace
{
constexpr size_t historyShownMax = 120;   // the Python page's tail: the ring itself is up to 10 000
constexpr int everyDevice = 1;

}

MonitorTrafficPage::MonitorTrafficPage (CoreClient& c)
    : client (c),
      table ({ { "DIR", 0.5f }, { "ADDRESS", 3.0f }, { "PEER", 1.2f }, { "COUNT", 0.9f, true }, { "/S", 0.7f, true },
               { "LAST VALUE", 2.0f }, { "AGE", 0.8f, true } }),
      historyTable ({ { "AGE", 0.8f, true }, { "DIR", 0.5f }, { "PEER", 1.2f }, { "ADDRESS", 3.0f }, { "VALUE", 2.0f }, { "TYPE", 0.6f } })
{
    styleField (search, "filter by address");
    search.onTextChange = [this] { filterChanged(); };
    onlyActive.onClick = [this] { filterChanged(); };
    device.addItem ("every device", everyDevice);
    device.setSelectedId (everyDevice, juce::dontSendNotification);
    device.onChange = [this] { filterChanged(); };
    look::caption (status);
    look::caption (historyTitle);
    table.onSelect = [this] (int row) {
        if (rebuilding)
            return;
        if (row >= 0 && row < (int) shown.size())
        {
            pickedDirection = shown[(size_t) row].direction;
            pickedAddress = shown[(size_t) row].address;
        }
        else
            pickedDirection = pickedAddress = {};
        copyFromClient();   // that row's history
        showHistory();
    };
    for (auto* child : std::initializer_list<juce::Component*> { &search, &onlyActive, &device, &status, &historyTitle, &table, &historyTable })
        addAndMakeVisible (child);
    refresh();
}

MonitorTrafficPage::~MonitorTrafficPage() = default;

void MonitorTrafficPage::refresh()
{
    if (copied > 0 && client.trafficGeneration() == seen)
        return;   // nothing new: the 20 Hz tick costs a comparison
    copyFromClient();
    rebuild();
}

void MonitorTrafficPage::copyFromClient()
{
    report = client.glance (pickedDirection, pickedAddress, historyShownMax);
    seen = report.generation;
    ++copied;
}

TrafficFilter MonitorTrafficPage::filter() const { return current; }

void MonitorTrafficPage::setFilter (const TrafficFilter& wanted)
{
    current = wanted;
    search.setText (utf8 (wanted.text), juce::dontSendNotification);
    onlyActive.setToggleState (wanted.onlyActive, juce::dontSendNotification);
    rebuild();
}

void MonitorTrafficPage::filterChanged()
{
    current.text = search.getText().toStdString();
    current.onlyActive = onlyActive.getToggleState();
    const int id = device.getSelectedId();
    current.device = id > everyDevice && id - 2 < (int) peers.size() ? peers[(size_t) (id - 2)] : std::string();
    rebuild();
}

void MonitorTrafficPage::rebuild()
{
    // The device choices are the peers the traffic names, plus a chosen one that has gone quiet.
    auto names = peersOf (report.rows);
    if (! current.device.empty() && std::find (names.begin(), names.end(), current.device) == names.end())
        names.push_back (current.device);
    if (names != peers)
    {
        peers = names;
        device.clear (juce::dontSendNotification);
        device.addItem ("every device", everyDevice);
        for (size_t i = 0; i < peers.size(); ++i)
            device.addItem (utf8 (peers[i]), (int) i + 2);
    }
    const auto chosen = std::find (peers.begin(), peers.end(), current.device);
    device.setSelectedId (current.device.empty() || chosen == peers.end() ? everyDevice : (int) (chosen - peers.begin()) + 2,
                          juce::dontSendNotification);

    shown = filtered (report.rows, current);
    std::sort (shown.begin(), shown.end(), [] (const TrafficRow& a, const TrafficRow& b) {
        return std::tie (a.address, a.direction) < std::tie (b.address, b.direction);
    });
    int picked = -1;
    for (size_t i = 0; i < shown.size(); ++i)
        if (shown[i].direction == pickedDirection && shown[i].address == pickedAddress)
            picked = (int) i;

    rebuilding = true;
    table.show ((int) shown.size(),
                [this] (int row, int column) -> juce::String {
                    const auto& r = shown[(size_t) row];
                    switch (column)
                    {
                        case 0: return utf8 (r.direction);
                        case 1: return utf8 (r.address);
                        case 2: return utf8 (r.peer);
                        case 3: return juce::String (r.count);
                        case 4: return rateText (r.rate);
                        case 5: return utf8 (r.lastValue);
                        case 6: return ageText (r.age);
                        default: return {};
                    }
                },
                [this] (int row, int column) {
                    if (column == 6)
                        return ageTone (shown[(size_t) row].age);
                    return column == 1 ? palette::text : palette::textMuted;
                });
    table.select (picked);
    rebuilding = false;

    if (! report.answered)
        look::say (status, client.summary().answered ? "waiting for Core's traffic" : "no Core at " + client.url());
    else
        look::say (status, juce::String ((int) shown.size()) + " of " + juce::String ((int) report.rows.size()) + " addresses; "
                               + juce::String (report.unknownAddresses) + " not understood");
    showHistory();
}

void MonitorTrafficPage::selectRow (int row)
{
    if (row < 0 || row >= (int) shown.size())
        pickedDirection = pickedAddress = {};
    else
    {
        pickedDirection = shown[(size_t) row].direction;
        pickedAddress = shown[(size_t) row].address;
    }
    rebuilding = true;
    table.select (row);
    rebuilding = false;
    copyFromClient();   // that row's history
    showHistory();
}

std::vector<HistoryEntry> MonitorTrafficPage::historyShown() const { return report.history; }

void MonitorTrafficPage::showHistory()
{
    look::say (historyTitle, pickedAddress.empty() ? juce::String ("HISTORY") : "HISTORY - " + utf8 (pickedDirection + " " + pickedAddress));
    historyTable.show ((int) report.history.size(), [this] (int row, int column) -> juce::String {
        const auto& e = report.history[(size_t) row];
        switch (column)
        {
            case 0: return ageText (report.at - e.at);
            case 1: return utf8 (e.direction);
            case 2: return utf8 (e.peer);
            case 3: return utf8 (e.address);
            case 4: return utf8 (e.value);
            case 5: return utf8 (e.type);
            default: return {};
        }
    });
}

void MonitorTrafficPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void MonitorTrafficPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 60);
    const int row = look::rowHeight (*this);
    const int gap = row / 4;
    auto bar = area.removeFromTop (row);
    search.setBounds (bar.removeFromLeft (bar.getWidth() / 3).reduced (0, gap / 2));
    fitField (search);
    bar.removeFromLeft (gap);
    onlyActive.setBounds (bar.removeFromLeft (bar.getWidth() / 4));
    device.setBounds (bar.removeFromLeft (bar.getWidth() / 3).reduced (0, gap / 2));
    bar.removeFromLeft (gap);
    status.setBounds (bar);
    area.removeFromTop (gap);
    table.setBounds (area.removeFromTop (area.getHeight() * 3 / 5));
    area.removeFromTop (gap);
    historyTitle.setBounds (area.removeFromTop (row));
    historyTable.setBounds (area);
}
}
