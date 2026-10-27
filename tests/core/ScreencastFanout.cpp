// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_screencast_fanout.py (B0 f3345dc7feeb): one encoder serves
// several viewers, and a viewer that falls behind is dropped, never waited for.
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <future>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "Net.h"
#include "Platform.h"
#include "ScreencastFanout.h"

using namespace a3core;

namespace
{
constexpr double deadline = 5.0;

using Steady = std::chrono::steady_clock;

template <typename Condition>
bool waitUntil (Condition condition, double seconds = deadline)
{
    const auto end = Steady::now() + std::chrono::duration<double> (seconds);
    while (Steady::now() < end)
    {
        if (condition())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }
    return condition();
}

std::vector<std::string> sh (const std::string& script) { return { "/bin/sh", "-c", script }; }

const std::string fakeEncoder = "while :; do printf TS; sleep 0.01; done";
const std::string shortEncoder = "printf TS; sleep 0.1";
const std::string deafEncoder = "trap '' TERM; while :; do printf TS; sleep 0.01; done";
const std::string firehoseEncoder = "while :; do head -c 65536 /dev/zero; sleep 0.001; done";
const std::string eofEncoder = "printf TS; exec 1>&-; sleep 1.5";

Chunk chunkOf (const std::string& bytes) { return std::make_shared<const std::string> (bytes); }
}

namespace
{
// runFanout on a thread of its own, on a port the system chose, with short timings.
class FanoutFixture : public ::testing::Test
{
protected:
    void run (const std::string& script, double linger = 10, size_t limitBytes = 4 * 1024 * 1024, double restartAfter = 0.1)
    {
        FanoutOptions options;
        options.restartAfter = restartAfter;
        options.killAfter = 0.2;
        options.limitBytes = limitBytes;
        options.platform = platform;
        std::promise<int> listening;
        auto portReady = listening.get_future();
        options.onListening = [&listening] (int bound) { listening.set_value (bound); };
        options.onEncoderStarted = [this] (int pid) { std::lock_guard<std::mutex> lock (mutex); started.push_back (pid); };
        options.onEncoderEnded = [this] (int pid, int status) { std::lock_guard<std::mutex> lock (mutex); ended.push_back ({ pid, status }); };
        options.onEncoderRead = [this] { ++reads; };
        options.onEncoderChunk = [this] (const std::string& chunk) { std::lock_guard<std::mutex> lock (mutex); capacities.push_back ({ chunk.size(), chunk.capacity() }); };
        ran = true;
        thread = std::thread ([this, script, linger, options] { result = runFanout (0, sh (script), linger, stop, options); });
        ASSERT_EQ (portReady.wait_for (std::chrono::seconds (5)), std::future_status::ready);
        port = portReady.get();
    }

    void TearDown() override
    {
        stop = true;
        if (thread.joinable())
            thread.join();
        for (const int client : clients)
            ::close (client);
        if (ran)
            EXPECT_EQ (result, 0);
        // Nothing leaks: every encoder that was started has been reaped by the time the loop returns.
        for (const int pid : started)
            EXPECT_EQ (::kill (pid, 0), -1) << "encoder " << pid << " still exists";
    }

    int connectClient (int receiveBuffer = 0)
    {
        const int fd = ::socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (receiveBuffer > 0)
            ::setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &receiveBuffer, sizeof receiveBuffer);
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_port = htons (static_cast<uint16_t> (port));
        ::inet_pton (AF_INET, loopbackAddress().c_str(), &address.sin_addr);
        EXPECT_EQ (::connect (fd, reinterpret_cast<sockaddr*> (&address), sizeof address), 0);
        clients.push_back (fd);
        return fd;
    }

    // Up to n bytes, or fewer at the end of the stream or after `seconds` of silence.
    static std::string receive (int fd, size_t n, double seconds = deadline)
    {
        std::string got;
        char buffer[65536];
        while (got.size() < n)
        {
            pollfd p { fd, POLLIN, 0 };
            if (::poll (&p, 1, static_cast<int> (seconds * 1000)) <= 0)
                break;
            const auto count = ::recv (fd, buffer, std::min (sizeof buffer, n - got.size()), 0);
            if (count <= 0)
                break;
            got.append (buffer, static_cast<size_t> (count));
        }
        return got;
    }

    size_t startedCount()
    {
        std::lock_guard<std::mutex> lock (mutex);
        return started.size();
    }

    size_t endedCount()
    {
        std::lock_guard<std::mutex> lock (mutex);
        return ended.size();
    }

    std::mutex mutex;
    std::vector<int> started;
    std::vector<std::pair<int, int>> ended;
    std::vector<std::pair<size_t, size_t>> capacities;   // size, capacity of each chunk read
    std::atomic<int> reads { 0 };
    std::vector<int> clients;
    std::atomic<bool> stop { false };
    std::thread thread;
    int port = 0;
    int result = -1;
    bool ran = false;
    const Platform* platform = nullptr;   // none: the loop takes this machine's
};

// This machine's platform, counting what it was asked to start.
class CountingPlatform final : public Platform
{
public:
    std::optional<std::string> environment (const std::string& name) const override { return real->environment (name); }
    juce::File home() const override { return real->home(); }
    juce::File stateDir() const override { return real->stateDir(); }
    juce::File configDir() const override { return real->configDir(); }
    juce::File packageShare() const override { return real->packageShare(); }
    std::string hostname() const override { return real->hostname(); }
    bool unitEnabled (const std::string& unit) const override { return real->unitEnabled (unit); }
    bool unitActive (const std::string& unit) const override { return real->unitActive (unit); }
    juce::File self() const override { return real->self(); }
    std::unique_ptr<Child> spawn (const std::vector<std::string>& argv, bool pipeStdout, ProcessGroup group = ProcessGroup::inherit, ErrorOutput errors = ErrorOutput::inherit) const override
    {
        std::lock_guard<std::mutex> lock (mutex);
        asked.push_back ({ argv, pipeStdout });
        return real->spawn (argv, pipeStdout, group, errors);
    }
    std::vector<std::pair<std::vector<std::string>, bool>> spawns() const
    {
        std::lock_guard<std::mutex> lock (mutex);
        return asked;
    }

private:
    std::unique_ptr<Platform> real = makePlatform();
    mutable std::mutex mutex;
    mutable std::vector<std::pair<std::vector<std::string>, bool>> asked;
};
}


// The suite is one fixture: gtest does not mix TEST and TEST_F under one name.
class ScreencastFanout : public FanoutFixture
{
};

TEST_F (ScreencastFanout, EveryViewerGetsEveryChunk)
{
    Fanout fanout;
    fanout.add (1);
    fanout.add (2);
    fanout.feed ("ab");
    EXPECT_EQ (fanout.pending (1), "ab");
    EXPECT_EQ (fanout.pending (2), "ab");
}

TEST_F (ScreencastFanout, SentBytesLeaveTheQueue)
{
    Fanout fanout;
    fanout.add (1);
    fanout.feed ("abcd");
    fanout.sent (1, 3);
    EXPECT_EQ (fanout.pending (1), "d");
}

TEST_F (ScreencastFanout, ASlowViewerIsDroppedNotWaitedFor)
{
    Fanout fanout (10);
    fanout.add (1);   // slow
    fanout.add (2);   // quick
    fanout.feed (std::string (5, 'x'));
    fanout.sent (2, 5);
    const auto dropped = fanout.feed (std::string (6, 'x'));
    EXPECT_EQ (dropped, std::vector<Viewer> { 1 });
    EXPECT_EQ (fanout.viewers(), std::vector<Viewer> { 2 });
}

TEST_F (ScreencastFanout, TheChunkIsSharedBetweenViewersNotCopied)
{
    Fanout fanout;
    fanout.add (1);
    fanout.add (2);
    const auto chunk = chunkOf (std::string (200, 'a'));
    fanout.feed (chunk);
    EXPECT_EQ (fanout.pending (1).data(), chunk->data());
    EXPECT_EQ (fanout.pending (2).data(), chunk->data());
}

TEST_F (ScreencastFanout, HasPendingFollowsTheBacklog)
{
    Fanout fanout;
    fanout.add (1);
    EXPECT_FALSE (fanout.hasPending (1));
    fanout.feed ("abc");
    EXPECT_TRUE (fanout.hasPending (1));
    fanout.sent (1, 2);
    EXPECT_TRUE (fanout.hasPending (1));
    fanout.sent (1, 1);
    EXPECT_FALSE (fanout.hasPending (1));
}

TEST_F (ScreencastFanout, PartialSendsWalkAcrossChunks)
{
    Fanout fanout;
    fanout.add (1);
    fanout.feed ("abc");
    fanout.feed ("def");
    fanout.sent (1, 2);
    EXPECT_EQ (fanout.pending (1), "c");
    fanout.sent (1, 1);
    EXPECT_EQ (fanout.pending (1), "def");
}

TEST_F (ScreencastFanout, TheDropThresholdIsExact)
{
    Fanout fanout (10);
    fanout.add (1);
    EXPECT_TRUE (fanout.feed (std::string (10, 'x')).empty());
    EXPECT_EQ (fanout.feed ("x"), std::vector<Viewer> { 1 });

    Fanout other (10);
    other.add (1);
    EXPECT_EQ (other.feed (std::string (11, 'x')), std::vector<Viewer> { 1 });
}

TEST_F (ScreencastFanout, ANinthViewerIsRefused)
{
    Fanout fanout (4 * 1024 * 1024, 8);
    std::vector<bool> results;
    for (Viewer viewer = 1; viewer <= 9; ++viewer)
        results.push_back (fanout.add (viewer));
    EXPECT_EQ (results, (std::vector<bool> { true, true, true, true, true, true, true, true, false }));
}

TEST_F (ScreencastFanout, TheEncoderStartsWithTheFirstViewerAndLingers)
{
    EncoderGate gate (10);
    EXPECT_FALSE (gate.wanted (0, 0));
    EXPECT_TRUE (gate.wanted (1, 1));
    EXPECT_TRUE (gate.wanted (0, 5));
    EXPECT_FALSE (gate.wanted (0, 11.1));
}

TEST_F (ScreencastFanout, ASecondViewerCostsNoSecondEncoder)
{
    run (fakeEncoder);
    const int a = connectClient();
    const int b = connectClient();
    EXPECT_EQ (receive (a, 2), "TS");
    EXPECT_EQ (receive (b, 2), "TS");
    EXPECT_EQ (startedCount(), 1u);
}

TEST_F (ScreencastFanout, TheEncoderStopsWhenTheLastViewerHasGone)
{
    run (fakeEncoder, 0.2);
    const int a = connectClient();
    const int b = connectClient();
    receive (a, 2);
    receive (b, 2);
    ::close (a);
    ::close (b);
    clients.clear();
    EXPECT_TRUE (waitUntil ([this] { return endedCount() == 1u; }));
    EXPECT_EQ (startedCount(), 1u);
}

TEST_F (ScreencastFanout, AnEncoderThatExitsIsStartedAgainWhileAViewerWaits)
{
    run (shortEncoder);
    const int viewer = connectClient();
    EXPECT_EQ (receive (viewer, 2), "TS");
    EXPECT_TRUE (waitUntil ([this] { return startedCount() >= 2u; }));
    EXPECT_EQ (receive (viewer, 2), "TS");
}

TEST_F (ScreencastFanout, AnEncoderAtEofIsNotPolledInABusyLoop)
{
    run (eofEncoder);
    const int viewer = connectClient();
    EXPECT_EQ (receive (viewer, 2), "TS");
    std::this_thread::sleep_for (std::chrono::milliseconds (800));
    EXPECT_LT (reads.load(), 20);
}

TEST_F (ScreencastFanout, AnEncoderThatIgnoresTerminateIsKilled)
{
    run (deafEncoder, 0.1);
    const int viewer = connectClient();
    receive (viewer, 2);
    ::close (viewer);
    clients.clear();
    ASSERT_TRUE (waitUntil ([this] { return endedCount() == 1u; }));
    std::lock_guard<std::mutex> lock (mutex);
    ASSERT_TRUE (WIFSIGNALED (ended[0].second));
    EXPECT_EQ (WTERMSIG (ended[0].second), SIGKILL);
}

TEST_F (ScreencastFanout, AViewerThatNeverReadsIsDroppedAndTheOtherKeepsReceiving)
{
    run (firehoseEncoder, 10, 256 * 1024);
    const int slow = connectClient (4096);
    const int quick = connectClient();
    std::atomic<size_t> received { 0 };
    std::atomic<bool> draining { true };
    std::thread drain ([&] {
        char buffer[65536];
        while (draining)
        {
            pollfd p { quick, POLLIN, 0 };
            if (::poll (&p, 1, 50) <= 0)
                continue;
            const auto count = ::recv (quick, buffer, sizeof buffer, 0);
            if (count <= 0)
                return;
            received += static_cast<size_t> (count);
        }
    });

    const auto closedByServer = [slow] {
        char buffer[1 << 16];
        pollfd p { slow, POLLIN, 0 };
        if (::poll (&p, 1, 50) <= 0)
            return false;
        const auto count = ::recv (slow, buffer, sizeof buffer, 0);
        return count == 0 || (count < 0 && errno == ECONNRESET);
    };
    EXPECT_TRUE (waitUntil (closedByServer, 15));
    const auto before = received.load();
    EXPECT_TRUE (waitUntil ([&] { return received.load() > before + 65536; }));
    draining = false;
    drain.join();
}

TEST_F (ScreencastFanout, ANinthViewerIsClosedAndTheEightStay)
{
    run (fakeEncoder);
    std::vector<int> eight;
    for (int i = 0; i < 8; ++i)
        eight.push_back (connectClient());
    for (const int c : eight)
        EXPECT_EQ (receive (c, 2), "TS");
    const int ninth = connectClient();
    char byte;
    pollfd p { ninth, POLLIN, 0 };
    ASSERT_GT (::poll (&p, 1, 5000), 0);
    EXPECT_EQ (::recv (ninth, &byte, 1, 0), 0);
    for (const int c : eight)
        EXPECT_EQ (receive (c, 2), "TS");
    EXPECT_EQ (startedCount(), 1u);
}

TEST_F (ScreencastFanout, AChunkHoldsWhatWasRead)
{
    run (eofEncoder);
    const int viewer = connectClient();
    EXPECT_EQ (receive (viewer, 2), "TS");
    std::lock_guard<std::mutex> lock (mutex);
    ASSERT_FALSE (capacities.empty());
    EXPECT_EQ (capacities[0].first, 2u);
    EXPECT_LE (capacities[0].second, capacities[0].first + 64);   // not the 64 KiB read buffer
}

TEST_F (ScreencastFanout, TwoViewersOneEncoder)
{
    run ("yes A3 | head -c 1000000", 10, 4 * 1024 * 1024, 30.0);
    const int first = connectClient();
    const int second = connectClient();
    const auto all = receive (first, 1000000);
    EXPECT_EQ (all.size(), 1000000u);
    for (size_t i = 0; i < all.size(); ++i)
        ASSERT_EQ (all[i], "A3\n"[i % 3]) << "at " << i;
    const auto some = receive (second, 1000000, 1.0);
    EXPECT_FALSE (some.empty());
    EXPECT_EQ (some.find_first_not_of ("A3\n"), std::string::npos);
    EXPECT_EQ (startedCount(), 1u);
}

// Carried (d): the encoder is started (and so signalled and reaped) through the platform seam (O7).
TEST_F (ScreencastFanout, TheEncoderIsStartedThroughThePlatform)
{
    CountingPlatform counting;
    platform = &counting;
    run (fakeEncoder);
    const int viewer = connectClient();
    EXPECT_EQ (receive (viewer, 2), "TS");
    const auto spawns = counting.spawns();
    ASSERT_EQ (spawns.size(), 1u);
    EXPECT_EQ (spawns[0].first, sh (fakeEncoder));
    EXPECT_TRUE (spawns[0].second);   // its stdout is the stream
    stop = true;
    thread.join();
    platform = nullptr;
}
