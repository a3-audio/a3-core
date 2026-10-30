// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Devices.h"

#include "Json.h"

namespace a3core
{
std::string truthHash (const juce::File& file)
{
    juce::MemoryBlock bytes;
    file.loadFileAsData (bytes);
    return sha256Hex (std::string (static_cast<const char*> (bytes.getData()), bytes.getSize()));
}

bool Devices::heard (const std::string& device, const std::string& theirHash, double now, const std::string& host)
{
    const std::lock_guard<std::mutex> guard (lock);
    const auto before = heardFrom.find (device);
    const bool news = before == heardFrom.end() || before->second.hash != theirHash;
    heardFrom[device] = { theirHash, now, host };
    return news;
}

void Devices::reported (const std::string& device, const std::string& host, DeviceReport report)
{
    const std::lock_guard<std::mutex> guard (lock);
    reportsFrom[{ device, host }] = std::move (report);
}

namespace
{
juce::DynamicObject* snapshotRow (const std::string& device, const std::string& hash, bool matches, double age)
{
    auto* row = new juce::DynamicObject();
    row->setProperty ("device", juce::String (device));
    row->setProperty ("hash", juce::String (hash));
    row->setProperty ("matches", matches);
    row->setProperty ("age", age);
    return row;
}

void putReport (juce::DynamicObject& row, const DeviceReport* report)
{
    const auto textOrNull = [report] (const std::string DeviceReport::*field) {
        return report != nullptr ? juce::var (juce::String::fromUTF8 ((report->*field).c_str())) : juce::var();
    };
    row.setProperty ("version", textOrNull (&DeviceReport::version));
    row.setProperty ("firmware", textOrNull (&DeviceReport::firmware));
    row.setProperty ("audio", textOrNull (&DeviceReport::audio));
}
}

juce::var Devices::snapshot (double now) const
{
    std::map<std::string, Heard> copy;
    {
        const std::lock_guard<std::mutex> guard (lock);
        copy = heardFrom;
    }
    juce::Array<juce::var> rows;
    for (const auto& [device, word] : copy)
        rows.add (juce::var (snapshotRow (device, word.hash, word.hash == ownHash, now - word.at)));
    return rows;
}

juce::var Devices::rigRows (double now) const
{
    const std::lock_guard<std::mutex> guard (lock);
    juce::Array<juce::var> rows;
    for (const auto& [device, word] : heardFrom)
    {
        auto* row = snapshotRow (device, word.hash, word.hash == ownHash, now - word.at);
        row->setProperty ("host", juce::String (word.host));
        const auto report = reportsFrom.find ({ device, word.host });
        putReport (*row, report != reportsFrom.end() ? &report->second : nullptr);
        rows.add (juce::var (row));
    }
    return rows;
}

juce::var Devices::reports (double now) const
{
    const std::lock_guard<std::mutex> guard (lock);
    juce::Array<juce::var> rows;
    for (const auto& [key, report] : reportsFrom)
    {
        auto* row = new juce::DynamicObject();
        row->setProperty ("device", juce::String::fromUTF8 (key.first.c_str()));
        row->setProperty ("host", juce::String (key.second));
        putReport (*row, &report);
        row->setProperty ("age", now - report.at);
        rows.add (juce::var (row));
    }
    return rows;
}
}
