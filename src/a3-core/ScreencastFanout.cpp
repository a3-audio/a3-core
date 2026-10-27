// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScreencastFanout.h"

#include "Platform.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

extern char** environ;

namespace a3core
{
namespace
{
constexpr size_t chunkBytes = 64 * 1024;
constexpr int tickMillis = 100;
constexpr int listenBacklog = 128;
}

Fanout::Fanout (size_t limitBytes, int maxViewersAllowed)
    : limit (limitBytes), maxViewers (static_cast<size_t> (std::max (0, maxViewersAllowed)))
{
}

bool Fanout::add (Viewer viewer)
{
    if (backlogs.size() >= maxViewers)
        return false;
    backlogs[viewer] = Backlog {};
    return true;
}

std::vector<Viewer> Fanout::feed (Chunk chunk)
{
    std::vector<Viewer> dropped;
    for (auto it = backlogs.begin(); it != backlogs.end();)
    {
        it->second.chunks.push_back (chunk);
        it->second.size += chunk->size();
        if (it->second.size > limit)
        {
            dropped.push_back (it->first);
            it = backlogs.erase (it);
        }
        else
            ++it;
    }
    return dropped;
}

std::vector<Viewer> Fanout::feed (const std::string& chunk)
{
    return feed (std::make_shared<const std::string> (chunk));
}

bool Fanout::hasPending (Viewer viewer) const { return backlogs.at (viewer).size > 0; }

std::string_view Fanout::pending (Viewer viewer) const
{
    const auto& backlog = backlogs.at (viewer);
    if (backlog.size == 0)
        return {};
    return std::string_view (*backlog.chunks.front()).substr (backlog.offset);
}

void Fanout::sent (Viewer viewer, size_t n)
{
    auto& backlog = backlogs.at (viewer);
    backlog.size -= n;
    while (n > 0)
    {
        const auto left = backlog.chunks.front()->size() - backlog.offset;
        if (n < left)
        {
            backlog.offset += n;
            return;
        }
        n -= left;
        backlog.chunks.pop_front();
        backlog.offset = 0;
    }
}

void Fanout::remove (Viewer viewer) { backlogs.erase (viewer); }

std::vector<Viewer> Fanout::viewers() const
{
    std::vector<Viewer> all;
    for (const auto& entry : backlogs)
        all.push_back (entry.first);
    return all;
}

bool EncoderGate::wanted (int viewerCount, double now)
{
    if (viewerCount > 0)
    {
        lastWatched = now;
        return true;
    }
    if (! lastWatched)
        return false;
    if (now - *lastWatched <= linger)
        return true;
    lastWatched.reset();
    return false;
}

namespace
{
using Steady = std::chrono::steady_clock;

double nowSeconds() { return std::chrono::duration<double> (Steady::now().time_since_epoch()).count(); }

void closeFd (int& fd)
{
    if (fd >= 0)
        ::close (fd);
    fd = -1;
}

// The encoder is started, signalled and reaped through the platform (O7): a juce::ChildProcess can
// neither be asked politely (kill() is SIGKILL only) nor read without blocking, and the loop needs both.
class Encoder
{
public:
    Encoder (std::vector<std::string> commandLine, const FanoutOptions& opts, const Platform& platformToUse)
        : command (std::move (commandLine)), options (opts), platform (platformToUse) {}

    int stdoutFd() const { return child != nullptr ? child->stdoutFd() : -1; }
    bool running() const { return child != nullptr; }
    bool reading() const { return stdoutFd() >= 0 && watching; }
    double restartAt = 0.0;

    void start (double now)
    {
        if (spawn())
            return;
        restartAt = now + options.restartAfter;   // a missing binary is retried, not fatal
    }

    void stopReading() { watching = false; }

    bool exited()
    {
        if (child == nullptr || reaped)
            return child != nullptr;
        const auto status = child->poll();
        if (! status)
            return false;
        finish (*status);
        return true;
    }

    void stop()
    {
        if (child == nullptr)
            return;
        if (! reaped)
        {
            child->terminate();
            if (const auto status = child->wait (options.killAfter))
                finish (*status);
            else
            {
                child->kill();
                finish (child->wait (-1.0).value_or (0));
            }
        }
        release();
    }

    void release()
    {
        child.reset();   // closes its stdout
        reaped = false;
        watching = false;
    }

private:
    bool spawn()
    {
        child = platform.spawn (command, true);
        if (child == nullptr)
            return false;
        reaped = false;
        watching = true;
        if (options.onEncoderStarted)
            options.onEncoderStarted (child->pid());
        return true;
    }

    void finish (int status)
    {
        reaped = true;
        if (options.onEncoderEnded)
            options.onEncoderEnded (child->pid(), status);
    }

    std::vector<std::string> command;
    const FanoutOptions& options;
    const Platform& platform;
    std::unique_ptr<Child> child;
    bool reaped = false;
    bool watching = false;
};

int openListener (int port)
{
    const int fd = ::socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;
    const int on = 1;
    ::setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl (INADDR_ANY);
    address.sin_port = htons (static_cast<uint16_t> (port));
    if (::bind (fd, reinterpret_cast<sockaddr*> (&address), sizeof address) != 0 || ::listen (fd, listenBacklog) != 0)
    {
        const int saved = errno;
        ::close (fd);
        errno = saved;
        return -1;
    }
    return fd;
}

int boundPort (int fd, int port)
{
    sockaddr_in address {};
    socklen_t length = sizeof address;
    if (::getsockname (fd, reinterpret_cast<sockaddr*> (&address), &length) != 0)
        return port;
    return ntohs (address.sin_port);
}

class Loop
{
public:
    Loop (int listener, Encoder& encoder, double linger, const FanoutOptions& opts)
        : listener (listener), encoder (encoder), gate (linger), fanout (opts.limitBytes, opts.maxViewers), options (opts) {}

    void pass()
    {
        auto fds = watched();
        ::poll (fds.data(), static_cast<nfds_t> (fds.size()), tickMillis);
        for (const auto& entry : fds)
            if (entry.revents != 0)
                dispatch (entry);
        steerEncoder();
    }

    void closeAll()
    {
        for (const auto viewer : fanout.viewers())
            close (viewer);
    }

private:
    std::vector<pollfd> watched() const
    {
        std::vector<pollfd> fds { { listener, POLLIN, 0 } };
        if (encoder.reading())
            fds.push_back ({ encoder.stdoutFd(), POLLIN, 0 });
        for (const auto viewer : fanout.viewers())
            fds.push_back ({ viewer, static_cast<short> (fanout.hasPending (viewer) ? POLLIN | POLLOUT : POLLIN), 0 });
        return fds;
    }

    void dispatch (const pollfd& entry)
    {
        if (entry.fd == listener)
            return accept();
        if (encoder.reading() && entry.fd == encoder.stdoutFd())
            return readEncoder();
        const auto all = fanout.viewers();
        if (std::find (all.begin(), all.end(), entry.fd) == all.end())
            return;
        if (entry.revents & (POLLIN | POLLHUP | POLLERR))
        {
            if (viewerGone (entry.fd))
                return close (entry.fd);
        }
        if (entry.revents & POLLOUT)
            write (entry.fd);
    }

    void accept()
    {
        const int viewer = ::accept4 (listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (viewer < 0)
            return;
        if (! fanout.add (viewer))
            ::close (viewer);
    }

    void readEncoder()
    {
        if (options.onEncoderRead)
            options.onEncoderRead();
        const auto n = ::read (encoder.stdoutFd(), readBuffer.data(), readBuffer.size());
        if (n < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                encoder.stopReading();
            return;
        }
        if (n == 0)
        {
            // EOF stays readable forever; exited() reaps the child on a later tick.
            encoder.stopReading();
            return;
        }
        // Exactly what was read, so the byte cap on a backlog is also a cap on its memory.
        auto chunk = std::make_shared<const std::string> (readBuffer.data(), static_cast<size_t> (n));
        if (options.onEncoderChunk)
            options.onEncoderChunk (*chunk);
        for (const auto viewer : fanout.feed (std::move (chunk)))
            ::close (viewer);
    }

    void write (Viewer viewer)
    {
        const auto view = fanout.pending (viewer);
        if (view.empty())
            return;
        const auto n = ::send (viewer, view.data(), view.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                close (viewer);
            return;
        }
        fanout.sent (viewer, static_cast<size_t> (n));
    }

    // A viewer sends nothing; readable means it closed (or sent junk).
    static bool viewerGone (Viewer viewer)
    {
        char scratch[256];
        const auto n = ::recv (viewer, scratch, sizeof scratch, MSG_DONTWAIT);
        if (n == 0)
            return true;
        if (n > 0)
            return false;
        return errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR;
    }

    void close (Viewer viewer)
    {
        fanout.remove (viewer);
        ::close (viewer);
    }

    void steerEncoder()
    {
        const auto now = nowSeconds();
        const bool wanted = gate.wanted (static_cast<int> (fanout.viewers().size()), now);
        if (encoder.exited())
        {
            encoder.release();
            encoder.restartAt = now + options.restartAfter;
        }
        if (wanted && ! encoder.running() && now >= encoder.restartAt)
            encoder.start (now);
        else if (! wanted && encoder.running())
            encoder.stop();
    }

    std::string readBuffer = std::string (chunkBytes, '\0');
    int listener;
    Encoder& encoder;
    EncoderGate gate;
    Fanout fanout;
    const FanoutOptions& options;
};
}

int runFanout (int port, std::vector<std::string> encoderCommand, double linger, std::atomic<bool>& stop,
               const FanoutOptions& options)
{
    int listener = openListener (port);
    if (listener < 0)
    {
        std::fprintf (stderr, "screencast fan-out: cannot listen on port %d: %s\n", port, std::strerror (errno));
        return 1;
    }
    if (options.onListening)
        options.onListening (boundPort (listener, port));

    std::unique_ptr<Platform> own;
    const Platform& platform = options.platform != nullptr ? *options.platform : *(own = makePlatform());
    Encoder encoder (std::move (encoderCommand), options, platform);
    Loop loop (listener, encoder, linger, options);
    while (! stop.load())
        loop.pass();

    encoder.stop();
    loop.closeAll();
    closeFd (listener);
    return 0;
}
}
