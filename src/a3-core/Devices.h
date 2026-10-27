// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_devices.py: which truth each device speaks, and whether it is Core's own.
#pragma once

#include <map>
#include <mutex>
#include <string>
#include <utility>

#include <juce_core/juce_core.h>

namespace a3core
{
// The sha256 of the file's bytes: a copy made by cp or scp hashes the same.
std::string truthHash (const juce::File& file);

class Devices
{
public:
    explicit Devices (std::string ownHash) : ownHash (std::move (ownHash)) {}

    // Keep what `device` said. True if it is news -- the first word, or a different one.
    bool heard (const std::string& device, const std::string& theirHash, double now);
    // [{device, hash, matches, age}] sorted by device.
    juce::var snapshot (double now) const;

    const std::string ownHash;

private:
    mutable std::mutex lock;
    std::map<std::string, std::pair<std::string, double>> heardFrom;
};
}
