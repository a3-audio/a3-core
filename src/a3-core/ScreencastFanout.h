// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// One encoder, several viewers: the screencast's TCP fan-out (port of a3_screencast_fanout.py).
//
// One external encoder writes MPEG-TS to its stdout and this loop copies it to every connected
// viewer. A viewer that falls behind is closed once its backlog passes the limit: waiting for it
// would stall the encoder and with it every other viewer. The encoder runs only while somebody
// watches: it starts with the first viewer and stops a while after the last one has gone.
#pragma once

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace a3core
{
using Viewer = int;   // the viewer's socket
using Chunk = std::shared_ptr<const std::string>;

// Per-viewer backlog of bytes not yet written. A chunk is shared between the backlogs, never copied.
class Fanout
{
public:
    Fanout (size_t limitBytes = 4 * 1024 * 1024, int maxViewers = 8);

    bool add (Viewer viewer);
    // Queue the chunk for everyone; return the viewers dropped for lagging.
    std::vector<Viewer> feed (Chunk chunk);
    std::vector<Viewer> feed (const std::string& chunk);
    bool hasPending (Viewer viewer) const;
    // The unsent rest of the oldest chunk (empty if nothing waits).
    std::string_view pending (Viewer viewer) const;
    void sent (Viewer viewer, size_t n);
    void remove (Viewer viewer);
    std::vector<Viewer> viewers() const;

private:
    struct Backlog
    {
        std::deque<Chunk> chunks;
        size_t offset = 0;   // bytes of chunks.front() already sent
        size_t size = 0;
    };

    size_t limit;
    size_t maxViewers;
    std::map<Viewer, Backlog> backlogs;
};

// Wanted from the first viewer until `linger` seconds after the last one.
class EncoderGate
{
public:
    explicit EncoderGate (double linger = 10.0) : linger (linger) {}
    bool wanted (int viewerCount, double now);

private:
    double linger;
    std::optional<double> lastWatched;
};

struct FanoutOptions
{
    double restartAfter = 2.0;   // an encoder that exited is not started again sooner
    double killAfter = 2.0;      // SIGTERM, then SIGKILL after this long
    size_t limitBytes = 4 * 1024 * 1024;
    int maxViewers = 8;

    // Seams for the tests; all optional, called on the serving thread.
    std::function<void (int port)> onListening;
    std::function<void (int pid)> onEncoderStarted;
    std::function<void (int pid, int waitStatus)> onEncoderEnded;
    std::function<void()> onEncoderRead;
};

// Listen on `port` on every interface (0 = the system chooses) and serve until `stop` is set.
// Returns 0 after a stop, 1 if the listener could not be opened (said on stderr).
int runFanout (int port, std::vector<std::string> encoderCommand, double linger, std::atomic<bool>& stop,
               const FanoutOptions& options = {});
}
