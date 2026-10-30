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

// What a device says it runs (device.version): "" where it has none (no firmware, no audio network).
struct DeviceReport
{
    std::string version, firmware, audio;
    double at = 0.0;
};

class Devices
{
public:
    explicit Devices (std::string ownHash) : ownHash (std::move (ownHash)) {}

    // Keep what `device` said, from `host`. True if it is news -- the first word, or a different one.
    bool heard (const std::string& device, const std::string& theirHash, double now, const std::string& host = {});
    // Keep what `device` at `host` runs; the newest per device and host.
    void reported (const std::string& device, const std::string& host, DeviceReport report);
    // [{device, hash, matches, age}] sorted by device: B1's /api/devices.
    juce::var snapshot (double now) const;
    // The snapshot's rows with the host of the last hello and what the device at that host reported
    // (version, firmware, audio; null where it has not): /api/rig's devices.
    juce::var rigRows (double now) const;
    // [{device, host, version, firmware, audio, age}] sorted by device and host: every report, also of
    // a device that says no hello (the analyzer) and of each host one name runs on (StemDeck's decks).
    juce::var reports (double now) const;

    const std::string ownHash;

private:
    struct Heard
    {
        std::string hash;
        double at = 0.0;
        std::string host;
    };

    mutable std::mutex lock;
    std::map<std::string, Heard> heardFrom;
    std::map<std::pair<std::string, std::string>, DeviceReport> reportsFrom;
};
}
