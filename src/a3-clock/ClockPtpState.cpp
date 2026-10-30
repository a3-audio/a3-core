// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockPtpState.h"

namespace a3clock
{
namespace
{
const std::string noState = "no PTP state from a3-ptp-state: ";
}

juce::File ptpStateFile() { return juce::File ("/run/a3/ptp-state.json"); }

PortReading readPtpState (const juce::File& file, double nowUnix)
{
    const auto path = file.getFullPathName().toStdString();
    if (! file.existsAsFile())
        return { std::nullopt, noState + path + " missing (is a3-ptp-state.timer running?)" };
    const auto v = juce::JSON::parse (file.loadFileAsString());
    const auto& at = v["writtenAtUnix"];
    if (! v.isObject() || ! (at.isInt() || at.isInt64() || at.isDouble()))
        return { std::nullopt, noState + path + " unreadable" };
    const double age = nowUnix - (double) at;
    if (age > ptpStateStaleSeconds)
        return { std::nullopt, noState + path + " is " + std::to_string ((long long) age) + " s old" };
    const auto state = v["portState"].toString().toStdString();
    if (state.empty())
        return { std::nullopt, noState + v["problem"].toString().toStdString() };
    return { state, {} };
}
}
