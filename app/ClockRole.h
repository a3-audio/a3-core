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
//
// The PHC is the audio port's (the truth's network.audio_port, where ptp4l@<port> runs), found in
// sysfs as PipeWire's clock.interface finds it; a --device that is not that clock is refused. One
// instance steers: it holds an flock on the PHC and on <runtime dir>/a3/clock.lock, and owns the
// status file; --observe takes no lock and writes no status. ptp4l's port state is asked through
// its read-only management socket with pmc, whose reply socket is under /tmp (a3-core's ptp4l@
// drop-in turns Debian's PrivateTmp off for it); a state nobody can read is reported as unverified,
// never as LOCKED.
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
    std::optional<double> maxAdjustmentPpm() override;   // PTP_CLOCK_GETCAPS
    std::string problem() const override { return lastProblem; }
    // An exclusive flock on the device: false (and said) when another clock role has it.
    bool claim();

private:
    void failed (const char* what);
    std::string path;
    int fd = -1;
    bool writable = false;
    std::string lastProblem;
};

std::vector<std::string> profilerCommand();

// "/dev/ptpN" of the interface (<sysClassNet>/<interface>/device/ptp/ptpN), or nullopt.
std::optional<std::string> phcOfInterface (const juce::File& sysClassNet, const std::string& interface);
// Why the role must not steer, or empty: no audio port, a port without a PHC, or a --device that is
// not the port's.
std::string phcRefusal (const std::string& audioPort, const std::optional<std::string>& portsPhc, const std::string& given);
// pmc's client socket for one query: /tmp/a3-clock-pmc.<pid>.<query>. Under /tmp because ptp4l@'s
// unit hides /run/user (ProtectHome); a3-core's drop-in shares /tmp with it (PrivateTmp=no).
juce::File pmcClientSocket (int pid, unsigned query);
// The command that asks ptp4l's read-only socket for the port data set through that client socket,
// started with umask 000: ptp4l's unit has no CAP_DAC_OVERRIDE and may only send to a socket others may write.
std::vector<std::string> pmcCommand (const juce::File& clientSocket);
// The portState word of pmc's answer to GET PORT_DATA_SET, or nullopt when nobody answered.
std::optional<std::string> portStateOf (const std::string& pmcOutput);
// The last locked frequency, kept in the state directory for a warm start after a boot.
std::optional<double> rememberedFrequency (const juce::File&);
bool rememberFrequency (const juce::File&, double ppm);

// ptp4l's port state through pmc on the read-only socket, without blocking: each call collects the
// answer to the last one and asks again.
class PmcPortState final : public a3clock::PtpPortState
{
public:
    explicit PmcPortState (const a3core::Platform&);
    ~PmcPortState() override;
    std::optional<std::string> portState() override;

private:
    void collect();
    const a3core::Platform& platform;
    juce::File socket;                 // the running query's, removed when it is collected
    unsigned queries = 0;
    std::unique_ptr<a3core::Child> child;
    std::string output;
    double startedAt = 0.0;
    std::optional<std::string> last;
};

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
    struct Plan
    {
        std::string device, interface, refusal;
    };
    static Plan planFor (const ProgramArgs&, const a3core::Platform&);
    bool takeInstanceLock();

    Plan plan;
    bool observe;
    juce::File statusFile, lockFile, stateFile;
    a3core::Journal journal;
    LinuxPhc phc;
    ProfilerProcess feed;
    PmcPortState port;
    int lockFd = -1;
    bool ownsLock = false;
    std::optional<int> exitCode;
    std::optional<a3clock::ClockDaemon> daemon;
};
}
