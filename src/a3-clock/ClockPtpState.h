// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// ptp4l's port state for the clock role. On the Core, Debian's sandboxed ptp4l@ unit never answers a
// user's pmc (measured on a3nuc1 with ptp4l 4.4, 2026-10-30), so a root timer (a3-ptp-state.timer,
// shipped by a3-core) asks it every 10 s and writes /run/a3/ptp-state.json; this reads that file. A
// file older than ptpStateStaleSeconds, missing or broken is no state: "PTP role: not read yet".
#pragma once

#include <optional>
#include <string>

#include <juce_core/juce_core.h>

namespace a3clock
{
constexpr double ptpStateStaleSeconds = 30.0;

struct PortReading
{
    std::optional<std::string> state;   // "MASTER", "SLAVE", ...; nullopt: not known
    std::string problem;                // why it is not known
};

juce::File ptpStateFile();
PortReading readPtpState (const juce::File&, double nowUnix);
}
