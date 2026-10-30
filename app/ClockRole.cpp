// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockRole.h"

#include <fcntl.h>
#include <sys/timex.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>

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

std::vector<std::string> profilerCommand() { return { "pw-profiler", "-J" }; }

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
juce::File statusFileOf (const a3core::Platform& platform)
{
    return a3clock::clockStatusFile (platform.environment ("XDG_RUNTIME_DIR"), (unsigned) getuid());
}

a3clock::DaemonOptions optionsOf (const ProgramArgs& args)
{
    a3clock::DaemonOptions options;
    options.device = args.clockDevice;
    options.observeOnly = args.clockObserve;
    return options;
}

double unixSeconds() { return (double) juce::Time::currentTimeMillis() / 1000.0; }
}

ClockRunner::ClockRunner (const ProgramArgs& args, const a3core::Platform& platform)
    : statusFile (statusFileOf (platform)),
      journal (a3core::streamJournal (stdout, stderr)),
      phc (args.clockDevice),
      feed (platform),
      daemon (phc, feed, optionsOf (args),
              [this] (const a3clock::ClockStatus& status) {
                  if (! a3clock::writeClockStatus (statusFile, status))
                      journal ("a3-core clock: cannot write " + statusFile.getFullPathName().toStdString(), true);
              },
              [this] (const std::string& line) { journal (line, false); }, monotonicSeconds())
{
}

ClockRunner::~ClockRunner() { statusFile.deleteFile(); }

std::optional<int> ClockRunner::tick (bool stopRequested)
{
    if (stopRequested)
        return 0;   // the PHC keeps the frequency it has: stopping is holdover, not a step
    daemon.tick (monotonicSeconds(), unixSeconds());
    return std::nullopt;
}
}
