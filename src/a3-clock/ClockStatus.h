// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What the clock role says about itself (ClockSteering.h says why it exists). It writes a status file
// in the user's runtime directory once an update; the engine reads it and serves it inside
// /api/engine/status, so the window's AUDIO page shows the lock without a port of its own. A file that
// is missing or older than clockStaleSeconds means the role is not running: the page warns.
// The soak test reads the journal line (logLine), one every ten seconds.
#pragma once

#include <optional>
#include <string>

#include <juce_core/juce_core.h>

#include "ClockSteering.h"

namespace a3clock
{
constexpr double clockStaleSeconds = 5.0;

struct ClockStatus
{
    std::string master = "scarlett-ptp";   // the clock master chosen (the only one in stage 1)
    Health health = Health::acquiring;
    std::string reason;
    std::string device;                    // the PHC steered
    std::string interface;                 // the audio port it belongs to
    std::string portState;                 // ptp4l's port state there ("MASTER"); empty: not known
    std::string portProblem;               // why it is not known
    bool observeOnly = false;
    double frequencyPpm = 0.0;             // what the PHC runs at against its free run
    double errorNs = 0.0;                  // PHC minus Scarlett, after anchoring
    std::optional<double> ratePpm;         // PHC against Scarlett over the last updates (0 when locked)
    double lockedSeconds = 0.0;            // since the current lock began; 0 when not locked
    SteeringCounters counters;
    std::int64_t scarlettBreaks = 0;       // Scarlett discontinuities seen in the profiler
    std::int64_t feedRestarts = 0;         // the profiler process started again
    double writtenAtUnix = 0.0;            // wall clock when written
};

juce::var toJson (const ClockStatus&);
std::optional<ClockStatus> clockStatusFromJson (const juce::var&);
std::optional<Health> healthFromWord (const juce::String&);

// <runtime dir>/a3/clock.json; without XDG_RUNTIME_DIR, systemd's /run/user/<uid>.
juce::File clockStatusFile (const std::optional<std::string>& runtimeDirectory, unsigned uid);
// Written whole (a temporary file renamed over it): a reader never sees half.
bool writeClockStatus (const juce::File&, const ClockStatus&);

// The status as the engine sees it: running, or why not.
struct ClockReport
{
    bool running = false;
    std::string why;
    ClockStatus status;
};
ClockReport readClockReport (const juce::File&, double nowUnix);
juce::var toJson (const ClockReport&);
// An absent or broken value is "not reported" (an engine from before the clock).
ClockReport clockReportFromJson (const juce::var&);

// The engine's reader: the file at most once a period (the engine publishes its status at 20 Hz).
class ClockReportReader
{
public:
    explicit ClockReportReader (juce::File, double periodSeconds = 1.0);
    const ClockReport& get (double nowSeconds, double nowUnix);

private:
    juce::File file;
    double period;
    std::optional<double> readAt;
    ClockReport last;
};

// What every surface shows of the clock in one word (the AUDIO page, the window's footer, Canvas
// through /api/engine/status). The maintainer, 2026-10-30: a clock that is synced says LOCKED. Only a
// real problem gets a warning word: not locked, or ptp4l known to be in another state than MASTER
// (the daemon then holds: HOLDOVER). Whether the role is confirmed is ptpRoleLine, a second line.
struct ClockIndicator
{
    enum class Tone { ok, caution, danger };
    std::string word;
    Tone tone = Tone::danger;
    std::string detail;
};
ClockIndicator clockIndicator (const ClockReport&);

// The AUDIO page's line under the state: "PTP grandmaster: confirmed", "PTP role: not read yet (...)"
// (neutral), or "PTP role: <state>, not the grandmaster" (a warning). Empty when the role is not running.
struct PtpRoleLine
{
    std::string text;
    bool warns = false;
};
PtpRoleLine ptpRoleLine (const ClockReport&);
juce::var toJson (const ClockIndicator&);

// The journal line: health, error, frequency, rate and every counter, on one line.
std::string logLine (const ClockStatus&);
}
