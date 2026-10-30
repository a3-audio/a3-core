// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The clock role (`a3-core --role clock`): the one clock's daemon on the Core (src/a3-clock/ClockSteering.h
// says why). Here is its Linux side: the PTP hardware clock through its character device, the Scarlett
// through `pw-profiler -J` as a child process, and the runner the app's timer drives.
//
// The role is not in the audio path: the profiler is a PipeWire client that reads the driver's clock,
// not a node, and the PHC is a network card's clock. If it dies, nothing audible happens at once: the
// PHC keeps its last frequency (holdover), and the engine reports the role as not running.
//
// Write access to the PHC is the device's group (`clock` on Debian: /dev/ptp* root:clock 0664), not a
// capability. Opened read-only, the role still measures, and a write is refused and said (fault).
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ClockDaemon.h"
#include "Platform.h"
#include "ProgramArgs.h"
#include "Roles.h"

namespace a3app
{
// The kernel's frequency unit (struct timex.freq): ppm with a 16-bit fraction.
long scaledPpm (double ppm);
double ppmOfScaled (long scaled);

class LinuxPhc final : public a3clock::PhcDevice
{
public:
    explicit LinuxPhc (std::string path);
    ~LinuxPhc() override;
    LinuxPhc (const LinuxPhc&) = delete;
    LinuxPhc& operator= (const LinuxPhc&) = delete;

    // The best of five MONOTONIC-PHC-MONOTONIC sandwiches (the narrowest).
    std::optional<a3clock::PhcSample> read() override;
    std::optional<double> frequencyPpm() override;
    bool setFrequencyPpm (double) override;
    std::string problem() const override { return lastProblem; }

private:
    void failed (const char* what);
    std::string path;
    int fd = -1;
    bool writable = false;
    std::string lastProblem;
};

std::vector<std::string> profilerCommand();

// A command's stdout as lines, from a child started through the platform (posix_spawn: a fork of a
// large process can stall audio).
class ProfilerProcess final : public a3clock::ProfilerFeed
{
public:
    ProfilerProcess (const a3core::Platform&, std::vector<std::string> command = profilerCommand());
    ~ProfilerProcess() override;
    bool start() override;
    bool drain (const std::function<void (std::string_view)>& line) override;

private:
    void stop();
    const a3core::Platform& platform;
    std::vector<std::string> command;
    std::unique_ptr<a3core::Child> child;
    std::string pending;   // a line not yet whole
    std::vector<char> buffer;
};

// The role: the daemon at 20 Hz, its status file in the runtime directory (removed on a clean stop, so
// the engine says "not running" at once), its journal on stdout.
class ClockRunner final : public RoleRunner
{
public:
    ClockRunner (const ProgramArgs&, const a3core::Platform&);
    ~ClockRunner() override;
    std::optional<int> tick (bool stopRequested) override;

private:
    juce::File statusFile;
    a3core::Journal journal;
    LinuxPhc phc;
    ProfilerProcess feed;
    a3clock::ClockDaemon daemon;
};
}
