// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_latest.py (B0 f3345dc7feeb): positions, the newest wins.
// The CoreReadsItsPortThisWay cases read a3-core.py's text and stay Python's.
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdexcept>

#include "Json.h"
#include "LatestWins.h"
#include "Truth.h"

using namespace a3core;

namespace
{
using Bytes = std::vector<uint8_t>;

// B0's shipped truth, from the canonical text Task 3 recorded.
const Truth& truth()
{
    static const Truth loaded = [] {
        juce::String error;
        const auto fixture = parseJson (juce::File (A3_CORE_TEST_DATA "/parity/json.json").loadFileAsString(), error);
        juce::MemoryOutputStream canonical;
        juce::Base64::convertFromBase64 (canonical, fixture["truth"]["canonical_b64"].toString());
        return Truth (parseJson (canonical.toString(), error));
    }();
    return loaded;
}

bool isPosition (const std::string& address)
{
    const auto found = truth().match (address);
    return found && (found->first == "channel.azimuth" || found->first == "channel.elevation");
}

std::string address (const std::string& key, int ch) { return truth().address (key, { { "ch", ch } }); }

Bytes packet (const std::string& addr, double value = 0.5) { return encodeMessage ({ addr, { pyFloat (value) } }); }

// As Motion sends a position: azimuth and elevation in one bundle (OscBundleBuilder(IMMEDIATELY)).
Bytes positionBundle (int ch, double az, double el)
{
    Bytes out { '#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    for (const auto& content : { packet (address ("channel.azimuth", ch), az), packet (address ("channel.elevation", ch), el) })
    {
        const auto n = (uint32_t) content.size();
        out.insert (out.end(), { (uint8_t) (n >> 24), (uint8_t) (n >> 16), (uint8_t) (n >> 8), (uint8_t) n });
        out.insert (out.end(), content.begin(), content.end());
    }
    return out;
}

std::optional<std::string> key (const Bytes& data) { return positionKey (data.data(), data.size(), isPosition); }

int udpSocket()
{
    const int fd = ::socket (AF_INET, SOCK_DGRAM, 0);
    sockaddr_in local {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    local.sin_port = 0;
    ::bind (fd, (sockaddr*) &local, sizeof local);
    return fd;
}

struct Sockets
{
    int receiver = udpSocket(), sender = udpSocket();
    ~Sockets() { ::close (receiver); ::close (sender); }

    void send (const Bytes& data) const
    {
        sockaddr_in to {};
        socklen_t length = sizeof to;
        ::getsockname (receiver, (sockaddr*) &to, &length);
        ::sendto (sender, data.data(), data.size(), 0, (sockaddr*) &to, sizeof to);
    }
    void sendBytes (int n) const
    {
        for (int i = 0; i < n; ++i)
            send ({ (uint8_t) i });
    }
};

std::function<bool()> passes (std::vector<bool> answers)
{
    auto left = std::make_shared<std::vector<bool>> (std::move (answers));
    return [left] {
        const bool next = left->front();
        left->erase (left->begin());
        return next;
    };
}

std::vector<Bytes> data (const std::vector<Datagram>& got)
{
    std::vector<Bytes> out;
    for (const auto& d : got)
        out.push_back (d.data);
    return out;
}

struct Loop
{
    Sockets s;
    std::vector<Bytes> handled;
    std::vector<std::string> reports;

    void runOnce (std::function<void (const Bytes&)> handle)
    {
        serve ({ s.receiver, [handle] (const Bytes& d, const Endpoint&) { handle (d); }, key, 100,
                 [this] (const std::string& r) { reports.push_back (r); }, passes ({ true, false }), {}, 0.1 });
    }
};
}

// WhichPacketIsAPosition
TEST (LatestWins, MotionsBundleIsAPosition) { EXPECT_TRUE (key (positionBundle (1, 10.0, 0.0)).has_value()); }

TEST (LatestWins, TwoChannelsAreTwoKeys) { EXPECT_NE (key (positionBundle (1, 0.0, 0.0)), key (positionBundle (2, 0.0, 0.0))); }

TEST (LatestWins, TheSameChannelIsTheSameKey) { EXPECT_EQ (key (positionBundle (3, 1.0, 2.0)), key (positionBundle (3, 5.0, 6.0))); }

TEST (LatestWins, AFaderIsNoPosition) { EXPECT_FALSE (key (packet (address ("channel.volume", 1))).has_value()); }

TEST (LatestWins, ADamagedPacketIsNoPosition)
{
    const Bytes garbage { 0x00, 0x01, 'g', 'a', 'r', 'b', 'a', 'g', 'e' };
    EXPECT_FALSE (key (garbage).has_value());
}

// TheNewestWins
TEST (LatestWins, OnlyTheLastPositionPerChannelIsHandled)
{
    std::vector<Bytes> packets;
    for (int i = 0; i < 5; ++i)
        packets.push_back (positionBundle (1, (double) i, 0.0));
    EXPECT_EQ (latestWins (packets, key), (std::vector<Bytes> { packets.back() }));
}

TEST (LatestWins, EverythingElseIsKeptInOrder)
{
    const auto fader = packet (address ("channel.volume", 2));
    const auto cue = packet (address ("channel.cue", 1));
    const auto p1 = positionBundle (1, 1.0, 0.0);
    const auto p2 = positionBundle (1, 2.0, 0.0);
    const auto q = positionBundle (2, 2.0, 0.0);
    EXPECT_EQ (latestWins ({ p1, fader, q, p2, cue }, key), (std::vector<Bytes> { fader, q, p2, cue }));
}

TEST (LatestWins, NothingWaitingIsNothing) { EXPECT_TRUE (latestWins ({}, key).empty()); }

// ReadingWhatWaits
TEST (LatestWins, ItTakesEverythingWaiting)
{
    Sockets s;
    s.sendBytes (7);
    std::vector<Bytes> expected;
    for (int i = 0; i < 7; ++i)
        expected.push_back ({ (uint8_t) i });
    EXPECT_EQ (data (drain (s.receiver, 100)), expected);
}

TEST (LatestWins, ItStopsAtTheLimit)
{
    Sockets s;
    s.sendBytes (7);
    EXPECT_EQ (drain (s.receiver, 3).size(), 3u);
    EXPECT_EQ (drain (s.receiver, 100).size(), 4u);
}

TEST (LatestWins, AnEmptySocketReturnsAtOnce)
{
    Sockets s;
    EXPECT_TRUE (drain (s.receiver, 100).empty());
}

// TheLoop
TEST (LatestWins, ABurstIsHandledNewestPositionFirstClass)
{
    Loop loop;
    const auto fader = packet (address ("channel.volume", 1));
    for (int i = 0; i < 5; ++i)
        loop.s.send (positionBundle (1, (double) i, 0.0));
    loop.s.send (fader);
    loop.runOnce ([&] (const Bytes& d) { loop.handled.push_back (d); });
    EXPECT_EQ (loop.handled, (std::vector<Bytes> { positionBundle (1, 4.0, 0.0), fader }));
}

TEST (LatestWins, AHandlerErrorIsReportedAndTheRestStillHandled)
{
    Loop loop;
    const Bytes bad { 'b', 'a', 'd' };
    loop.s.send (bad);
    loop.s.send (packet (address ("channel.volume", 2)));
    loop.runOnce ([&] (const Bytes& d) {
        if (d == bad)
            throw std::runtime_error ("boom");
        loop.handled.push_back (d);
    });
    EXPECT_EQ (loop.handled.size(), 1u);
    EXPECT_EQ (loop.reports.size(), 1u);
}

// TheLoopTicks
TEST (LatestWins, AnIdleLoopTicks)
{
    Sockets s;
    int ticks = 0;
    serve ({ s.receiver, [] (const Bytes&, const Endpoint&) {}, key, 100, [] (const std::string&) {},
             passes ({ true, true, false }), [&ticks] { ++ticks; }, 0.01 });
    EXPECT_EQ (ticks, 2);
}

TEST (LatestWins, ABrokenSocketStopsTheLoop)
{
    const int fd = udpSocket();
    ::close (fd);
    int passesLeft = 1000;   // a loop that does not stop on its own is stopped here, and fails
    std::vector<std::string> reports;
    serve ({ fd, [] (const Bytes&, const Endpoint&) {}, key, 100, [&] (const std::string& r) { reports.push_back (r); },
             [&] { return passesLeft-- > 0; }, [] {}, 0.001 });
    EXPECT_GT (passesLeft, 990);
    EXPECT_EQ (reports.size(), 1u);
}
