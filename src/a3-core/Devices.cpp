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

bool Devices::heard (const std::string& device, const std::string& theirHash, double now)
{
    const std::lock_guard<std::mutex> guard (lock);
    const auto before = heardFrom.find (device);
    const bool news = before == heardFrom.end() || before->second.first != theirHash;
    heardFrom[device] = { theirHash, now };
    return news;
}

juce::var Devices::snapshot (double now) const
{
    std::map<std::string, std::pair<std::string, double>> copy;
    {
        const std::lock_guard<std::mutex> guard (lock);
        copy = heardFrom;
    }
    juce::Array<juce::var> rows;
    for (const auto& [device, word] : copy)
    {
        auto* row = new juce::DynamicObject();
        row->setProperty ("device", juce::String (device));
        row->setProperty ("hash", juce::String (word.first));
        row->setProperty ("matches", word.first == ownHash);
        row->setProperty ("age", now - word.second);
        rows.add (juce::var (row));
    }
    return rows;
}
}
