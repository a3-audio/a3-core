// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockRole.h"

#include <fcntl.h>
#include <linux/ptp_clock.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/timex.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>

#include "ProgramArgs.h"
#include "Truth.h"

namespace a3app
{
namespace
{
// linux/posix-timers.h: the dynamic clock of an open PTP character device.
clockid_t clockOf (int fd) { return (clockid_t) ((((unsigned int) ~fd) << 3) | 3); }

std::int64_t nsOf (const timespec& t) { return (std::int64_t) t.tv_sec * 1'000'000'000LL + t.tv_nsec; }

constexpr int sandwiches = 5;
constexpr std::size_t readChunk = 16384;
constexpr std::size_t maxPendingBytes = 1 << 20;
constexpr double childStopSeconds = 1.0;
}

long scaledPpm (double ppm) { return std::lround (ppm * 65536.0); }
double ppmOfScaled (long scaled) { return (double) scaled / 65536.0; }

LinuxPhc::LinuxPhc (std::string devicePath) : path (std::move (devicePath))
{
    fd = ::open (path.c_str(), O_RDWR | O_CLOEXEC);
    writable = fd >= 0;
    if (fd < 0 && errno == EACCES)
        fd = ::open (path.c_str(), O_RDONLY | O_CLOEXEC);   // measure at least; a write is then refused and said
    if (fd < 0)
        failed ("cannot open");
}

LinuxPhc::~LinuxPhc()
{
    if (fd >= 0)
        ::close (fd);
}

void LinuxPhc::failed (const char* what) { lastProblem = path + ": " + what + ": " + std::strerror (errno); }

std::optional<a3clock::PhcSample> LinuxPhc::read()
{
    if (fd < 0)
        return std::nullopt;
    std::optional<a3clock::PhcSample> best;
    std::int64_t narrowest = 0;
    for (int i = 0; i < sandwiches; ++i)
    {
        timespec before {}, phc {}, after {};
        if (clock_gettime (CLOCK_MONOTONIC, &before) != 0 || clock_gettime (clockOf (fd), &phc) != 0
            || clock_gettime (CLOCK_MONOTONIC, &after) != 0)
        {
            failed ("cannot read");
            return std::nullopt;
        }
        const auto width = nsOf (after) - nsOf (before);
        if (! best || width < narrowest)
        {
            narrowest = width;
            best = a3clock::PhcSample { nsOf (before) + width / 2, nsOf (phc) };
        }
    }
    return best;
}

std::optional<double> LinuxPhc::frequencyPpm()
{
    if (fd < 0)
        return std::nullopt;
    timex asked {};
    if (clock_adjtime (clockOf (fd), &asked) < 0)
    {
        if (! writable)   // some kernels want write access even to read it: say the real cause
            lastProblem = path + ": opened read-only, no write access (is the user in the device's group, clock?)";
        else
            failed ("cannot read the frequency");
        return std::nullopt;
    }
    return ppmOfScaled (asked.freq);
}

bool LinuxPhc::setFrequencyPpm (double ppm)
{
    if (fd < 0)
        return false;
    if (! writable)
    {
        lastProblem = path + ": opened read-only, no write access (is the user in the device's group, clock?)";
        return false;
    }
    timex wanted {};
    wanted.modes = ADJ_FREQUENCY;   // the frequency only: this program never sets the PHC's time
    wanted.freq = scaledPpm (ppm);
    if (clock_adjtime (clockOf (fd), &wanted) < 0)
    {
        failed ("cannot set the frequency");
        return false;
    }
    return true;
}

std::optional<double> LinuxPhc::maxAdjustmentPpm()
{
    ptp_clock_caps caps {};
    if (fd < 0 || ioctl (fd, PTP_CLOCK_GETCAPS, &caps) != 0 || caps.max_adj <= 0)
        return std::nullopt;
    return caps.max_adj / 1000.0;   // ppb
}

bool LinuxPhc::claim()
{
    if (fd >= 0 && flock (fd, LOCK_EX | LOCK_NB) == 0)
        return true;
    lastProblem = path + (fd < 0 ? ": not open" : ": another clock role steers it already");
    return false;
}

std::vector<std::string> profilerCommand() { return { "pw-profiler", "-J" }; }

std::optional<std::string> phcOfInterface (const juce::File& sysClassNet, const std::string& interface)
{
    if (interface.empty())
        return std::nullopt;
    const auto ptp = sysClassNet.getChildFile (juce::String (interface)).getChildFile ("device/ptp");
    for (const auto& entry : ptp.findChildFiles (juce::File::findDirectories, false, "ptp*"))
        return "/dev/" + entry.getFileName().toStdString();
    return std::nullopt;
}

std::string phcRefusal (const std::string& audioPort, const std::optional<std::string>& portsPhc, const std::string& given)
{
    if (audioPort.empty())
        return "no audio port in the truth's network: there is no PTP clock to steer";
    if (! portsPhc)
        return audioPort + " has no PTP hardware clock: nothing to steer";
    if (! given.empty() && given != *portsPhc)
        return given + " is not " + audioPort + "'s PTP clock (that is " + *portsPhc + "): not steered";
    return {};
}

std::optional<std::string> portStateOf (const std::string& pmcOutput)
{
    const auto at = pmcOutput.find ("portState");
    if (at == std::string::npos)
        return std::nullopt;
    const auto start = pmcOutput.find_first_not_of (" \t", at + 9);
    const auto end = pmcOutput.find_first_of (" \t\r\n", start);
    if (start == std::string::npos || start == end)
        return std::nullopt;
    return pmcOutput.substr (start, end == std::string::npos ? std::string::npos : end - start);
}

std::optional<double> rememberedFrequency (const juce::File& file)
{
    if (! file.existsAsFile())
        return std::nullopt;
    const auto v = juce::JSON::parse (file.loadFileAsString());
    const auto& f = v["lockedFrequencyPpm"];
    if (! (f.isDouble() || f.isInt() || f.isInt64()))
        return std::nullopt;
    return (double) f;
}

bool rememberFrequency (const juce::File& file, double ppm)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("lockedFrequencyPpm", ppm);
    juce::TemporaryFile temporary (file);
    return file.getParentDirectory().createDirectory()
           && temporary.getFile().replaceWithText (juce::JSON::toString (juce::var (o), true))
           && temporary.overwriteTargetFileWithTemporary();
}

namespace
{
constexpr double pmcWaitSeconds = 3.0;
const char* const ptp4lReadOnlySocket = "/var/run/ptp4lro";
}

juce::File pmcClientSocket (int pid, unsigned query)
{
    return juce::File ("/tmp").getChildFile ("a3-clock-pmc." + juce::String (pid) + "." + juce::String (query));
}

std::vector<std::string> pmcCommand (const juce::File& clientSocket)
{
    return { "sh", "-c", "umask 000 && exec pmc \"$@\"", "pmc", "-u", "-b", "0", "-i", clientSocket.getFullPathName().toStdString(),
             "-s", ptp4lReadOnlySocket, "GET PORT_DATA_SET" };
}

PmcPortState::PmcPortState (const a3core::Platform& p) : platform (p) {}

PmcPortState::~PmcPortState()
{
    if (child && ! child->poll())
    {
        child->kill();
        child->wait (-1.0);
    }
    socket.deleteFile();
}

void PmcPortState::collect()
{
    char buffer[4096];
    for (ssize_t got; (got = ::read (child->stdoutFd(), buffer, sizeof buffer)) > 0;)
        output.append (buffer, (std::size_t) got);
}

std::optional<std::string> PmcPortState::portState()
{
    if (child)
    {
        collect();
        const bool ended = child->poll().has_value();
        if (! ended && monotonicSeconds() - startedAt < pmcWaitSeconds)
            return last;   // still asking; the answer comes with the next call
        if (! ended)
        {
            child->kill();
            child->wait (-1.0);
        }
        collect();
        last = portStateOf (output);
        child.reset();
        socket.deleteFile();   // pmc binds it and leaves it behind
    }
    output.clear();
    socket = pmcClientSocket ((int) getpid(), ++queries);
    socket.deleteFile();
    child = platform.spawn (pmcCommand (socket), true, a3core::ProcessGroup::inherit, a3core::ErrorOutput::discard);
    startedAt = monotonicSeconds();
    return last;
}

ProfilerProcess::ProfilerProcess (const a3core::Platform& p, std::vector<std::string> words)
    : platform (p), command (std::move (words)), buffer (readChunk)
{
}

ProfilerProcess::~ProfilerProcess() { stop(); }

void ProfilerProcess::stop()
{
    if (! child)
        return;
    if (! child->poll())
    {
        child->terminate();
        if (! child->wait (childStopSeconds))
        {
            child->kill();
            child->wait (-1.0);
        }
    }
    child.reset();
}

bool ProfilerProcess::start()
{
    stop();
    pending.clear();
    child = platform.spawn (command, true, a3core::ProcessGroup::inherit, a3core::ErrorOutput::discard);
    return child != nullptr;
}

bool ProfilerProcess::drain (const std::function<void (std::string_view)>& line)
{
    if (! child || child->stdoutFd() < 0)
        return false;
    for (;;)
    {
        const auto got = ::read (child->stdoutFd(), buffer.data(), buffer.size());
        if (got > 0)
        {
            pending.append (buffer.data(), (std::size_t) got);
            std::size_t start = 0;
            for (auto end = pending.find ('\n'); end != std::string::npos; end = pending.find ('\n', start))
            {
                line (std::string_view (pending).substr (start, end - start));
                start = end + 1;
            }
            pending.erase (0, start);
            if (pending.size() > maxPendingBytes)
                pending.clear();   // no line is this long: not the profiler's output
            continue;
        }
        if (got < 0 && errno == EINTR)
            continue;
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return ! child->poll().has_value();
        break;   // end of file, or an error: the child is gone
    }
    if (! pending.empty())
        line (pending);   // its last words, even without a newline
    pending.clear();
    stop();
    return false;
}

namespace
{
double unixSeconds() { return (double) juce::Time::currentTimeMillis() / 1000.0; }

juce::File runtimeFile (const a3core::Platform& platform, const char* name)
{
    return a3clock::clockStatusFile (platform.environment ("XDG_RUNTIME_DIR"), (unsigned) getuid()).getSiblingFile (name);
}
}

ClockRunner::Plan ClockRunner::planFor (const ProgramArgs& args, const a3core::Platform& platform)
{
    Plan plan;
    const auto files = coreFiles (platform);
    auto loaded = a3core::Truth::load (files.truth, files.network);
    if (const auto* truth = std::get_if<a3core::Truth> (&loaded))
        plan.interface = truth->network()["audio_port"].toString().toStdString();
    const auto portsPhc = phcOfInterface (juce::File ("/sys/class/net"), plan.interface);
    plan.refusal = phcRefusal (plan.interface, portsPhc, args.clockDevice);
    plan.device = args.clockDevice.empty() ? portsPhc.value_or ("") : args.clockDevice;
    return plan;
}

bool ClockRunner::takeInstanceLock()
{
    lockFile.getParentDirectory().createDirectory();
    lockFd = ::open (lockFile.getFullPathName().toRawUTF8(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    ownsLock = lockFd >= 0 && flock (lockFd, LOCK_EX | LOCK_NB) == 0;
    return ownsLock;
}

ClockRunner::ClockRunner (const ProgramArgs& args, const a3core::Platform& platform)
    : plan (planFor (args, platform)),
      observe (args.clockObserve),
      statusFile (a3clock::clockStatusFile (platform.environment ("XDG_RUNTIME_DIR"), (unsigned) getuid())),
      lockFile (runtimeFile (platform, "clock.lock")),
      stateFile (platform.stateDir().getChildFile ("clock.json")),
      journal (a3core::streamJournal (stdout, stderr)),
      phc (plan.device),
      feed (platform),
      port (platform)
{
    // One steering instance: it holds the lock and owns the status file. An observer takes neither.
    if (! observe && ! takeInstanceLock())
    {
        journal ("a3-core clock: another clock role is running (" + lockFile.getFullPathName().toStdString() + "); not starting", true);
        exitCode = 1;
        return;
    }
    if (! observe && plan.refusal.empty() && ! phc.claim())
    {
        journal ("a3-core clock: " + phc.problem() + "; not starting", true);
        exitCode = 1;
        return;
    }
    a3clock::DaemonOptions options;
    options.device = plan.device;
    options.interface = plan.interface;
    options.refusal = plan.refusal;
    options.observeOnly = observe;
    options.warmFrequencyPpm = rememberedFrequency (stateFile);
    options.intake.node = args.clockScarlett;
    a3clock::ClockDaemon::StatusSink sink;
    a3clock::ClockDaemon::Remember remember;
    if (! observe)
    {
        sink = [this] (const a3clock::ClockStatus& status) {
            if (! a3clock::writeClockStatus (statusFile, status))
                journal ("a3-core clock: cannot write " + statusFile.getFullPathName().toStdString(), true);
        };
        remember = [this] (double ppm) { rememberFrequency (stateFile, ppm); };
    }
    daemon.emplace (phc, feed, &port, options, std::move (sink), [this] (const std::string& line) { journal (line, false); },
                    std::move (remember), monotonicSeconds());
}

ClockRunner::~ClockRunner()
{
    daemon.reset();
    if (ownsLock)
        statusFile.deleteFile();   // only the lock's holder wrote it
    if (lockFd >= 0)
        ::close (lockFd);
}

std::optional<int> ClockRunner::tick (bool stopRequested)
{
    if (exitCode)
        return exitCode;
    if (stopRequested)
        return 0;   // the PHC keeps the frequency it has: stopping is holdover, not a step
    daemon->tick (monotonicSeconds(), unixSeconds());
    return std::nullopt;
}
}
