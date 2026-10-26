// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScreencastFanout.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
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

// A juce::ChildProcess can neither be asked politely (kill() is SIGKILL only) nor read without
// blocking, and the loop needs both; hence posix_spawn.
class Encoder
{
public:
    Encoder (std::vector<std::string> commandLine, const FanoutOptions& opts)
        : command (std::move (commandLine)), options (opts) {}

    int stdoutFd() const { return outFd; }
    bool running() const { return pid > 0; }
    bool reading() const { return outFd >= 0 && watching; }
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
        if (pid <= 0 || reaped)
            return pid > 0;
        int status = 0;
        if (::waitpid (pid, &status, WNOHANG) != pid)
            return false;
        finish (status);
        return true;
    }

    void stop()
    {
        if (pid <= 0)
            return;
        if (! reaped)
        {
            ::kill (pid, SIGTERM);
            if (! waitFor (options.killAfter))
            {
                ::kill (pid, SIGKILL);
                int status = 0;
                ::waitpid (pid, &status, 0);
                finish (status);
            }
        }
        release();
    }

    void release()
    {
        closeFd (outFd);
        pid = -1;
        reaped = false;
        watching = false;
    }

private:
    bool spawn()
    {
        int fds[2];
        if (::pipe2 (fds, O_CLOEXEC) != 0)
            return false;

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init (&actions);
        posix_spawn_file_actions_adddup2 (&actions, fds[1], 1);
        // Python's Popen closes every other descriptor (close_fds); a stray socket in the encoder
        // would keep a viewer's connection open after we closed it.
        posix_spawn_file_actions_addclosefrom_np (&actions, 3);

        std::vector<char*> argv;
        for (auto& word : command)
            argv.push_back (word.data());
        argv.push_back (nullptr);

        pid_t child = 0;
        const int error = command.empty() ? EINVAL : ::posix_spawnp (&child, argv[0], &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy (&actions);
        ::close (fds[1]);
        if (error != 0)
        {
            ::close (fds[0]);
            return false;
        }
        ::fcntl (fds[0], F_SETFL, ::fcntl (fds[0], F_GETFL) | O_NONBLOCK);
        pid = child;
        outFd = fds[0];
        reaped = false;
        watching = true;
        if (options.onEncoderStarted)
            options.onEncoderStarted (pid);
        return true;
    }

    bool waitFor (double seconds)
    {
        const auto end = Steady::now() + std::chrono::duration<double> (seconds);
        while (true)
        {
            int status = 0;
            if (::waitpid (pid, &status, WNOHANG) == pid)
            {
                finish (status);
                return true;
            }
            if (Steady::now() >= end)
                return false;
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
    }

    void finish (int status)
    {
        reaped = true;
        if (options.onEncoderEnded)
            options.onEncoderEnded (pid, status);
    }

    std::vector<std::string> command;
    const FanoutOptions& options;
    int pid = -1;
    int outFd = -1;
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

int boundPort (int fd)
{
    sockaddr_in address {};
    socklen_t length = sizeof address;
    ::getsockname (fd, reinterpret_cast<sockaddr*> (&address), &length);
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
        auto chunk = std::make_shared<std::string> (chunkBytes, '\0');
        const auto n = ::read (encoder.stdoutFd(), chunk->data(), chunk->size());
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
        chunk->resize (static_cast<size_t> (n));
        for (const auto viewer : fanout.feed (Chunk (std::move (chunk))))
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
        char scratch[chunkBytes];
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
        options.onListening (boundPort (listener));

    Encoder encoder (std::move (encoderCommand), options);
    Loop loop (listener, encoder, linger, options);
    while (! stop.load())
        loop.pass();

    encoder.stop();
    loop.closeAll();
    closeFd (listener);
    return 0;
}
}
