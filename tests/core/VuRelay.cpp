// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_vu_relay.py (B0 f3345dc7feeb): Core passes the analyzer's
// meters on to a remote Motion and to every desk it follows, unchanged and never parsed.
#include <chrono>
#include <cstring>
#include <memory>

#include <gtest/gtest.h>

#include "B0Truth.h"
#include "Render.h"
#include "VuRelay.h"

using namespace a3core;

namespace
{
Datagram bundle()
{
    const std::string head ("#bundle\0", 8);
    Datagram data (head.begin(), head.end());
    for (uint8_t i = 0; i < 40; ++i)
        data.push_back (i);
    return data;
}

Datagram bytes (const char* text) { return Datagram (text, text + std::strlen (text)); }

// running() for n passes of the loop, then stop.
std::function<bool()> times (int n)
{
    auto left = std::make_shared<int> (n);
    return [left] { return (*left)-- > 0; };
}

using Sent = std::vector<std::pair<Datagram, Endpoint>>;
using Send = std::function<void (const Datagram&, const Endpoint&)>;

Sent runWith (const std::vector<Datagram>& packets, const std::function<std::vector<Endpoint>()>& destinations, Send send = {})
{
    Sent sent;
    size_t next = 0;
    if (! send)
        send = [&sent] (const Datagram& data, const Endpoint& to) { sent.push_back ({ data, to }); };
    forward ([&]() -> std::optional<Datagram> { return packets.at (next++); }, send, destinations, times (static_cast<int> (packets.size())));
    return sent;
}

const Endpoint motion { "192.168.8.20", 7772 };
const Endpoint desk { "192.168.8.20", 7774 };
const Endpoint otherDesk { "192.168.8.30", 7774 };
}

TEST (VuRelay, GoesOnUnchangedToARemoteMotion)
{
    const auto sent = runWith ({ bundle(), bytes ("x") }, [] { return std::vector<Endpoint> { motion }; });
    ASSERT_EQ (sent.size(), 2u);
    EXPECT_EQ (sent[0], std::make_pair (bundle(), motion));
    EXPECT_EQ (sent[1], std::make_pair (bytes ("x"), motion));
}

TEST (VuRelay, IsDroppedWithNobodyToTell)
{
    EXPECT_TRUE (runWith ({ bundle() }, [] { return std::vector<Endpoint> {}; }).empty());
}

TEST (VuRelay, FollowsTheTargetsAsTheyMove)
{
    std::vector<std::vector<Endpoint>> targets { {}, { motion }, {} };
    size_t next = 0;
    const auto sent = runWith ({ bytes ("1"), bytes ("2"), bytes ("3") }, [&] { return targets.at (next++); });
    ASSERT_EQ (sent.size(), 1u);
    EXPECT_EQ (sent[0], std::make_pair (bytes ("2"), motion));
}

TEST (VuRelay, EveryDeskAndTheMotionGetEachPacket)
{
    const std::vector<Endpoint> to { motion, desk, otherDesk };
    const auto sent = runWith ({ bundle() }, [&] { return to; });
    ASSERT_EQ (sent.size(), 3u);
    for (size_t i = 0; i < 3; ++i)
        EXPECT_EQ (sent[i].second, to[i]);
}

TEST (VuRelay, OneGoneDeskDoesNotStarveTheOthers)
{
    std::vector<Endpoint> sent;
    runWith ({ bundle() }, [] { return std::vector<Endpoint> { otherDesk, desk }; },
             [&] (const Datagram&, const Endpoint& to) {
                 if (to.host == "192.168.8.30")
                     throw std::system_error (std::make_error_code (std::errc::host_unreachable));
                 sent.push_back (to);
             });
    ASSERT_EQ (sent.size(), 1u);
    EXPECT_EQ (sent[0], desk);
}

TEST (VuRelay, AFailedSendDoesNotStopTheRelay)
{
    // A remote Motion that went away is an ICMP error on the next send; the meters of the next one
    // to arrive must still flow.
    std::vector<Datagram> sent;
    runWith ({ bytes ("1"), bytes ("2") }, [] { return std::vector<Endpoint> { { "10.0.0.5", 1 } }; },
             [&] (const Datagram& data, const Endpoint&) {
                 if (data == bytes ("1"))
                     throw std::system_error (std::make_error_code (std::errc::connection_refused));
                 sent.push_back (data);
             });
    ASSERT_EQ (sent.size(), 1u);
    EXPECT_EQ (sent[0], bytes ("2"));
}

TEST (VuRelay, AnIdleReceiveAsksRunningAgain)
{
    int idle = 0;
    forward ([&]() -> std::optional<Datagram> { ++idle; return std::nullopt; },
             [] (const Datagram&, const Endpoint&) { ADD_FAILURE() << "nothing came in, nothing goes out"; },
             [] { return std::vector<Endpoint> { { "10.0.0.5", 1 } }; }, times (3));
    EXPECT_EQ (idle, 3);
}

TEST (VuRelay, TheBytesArriveAsTheyWereSent)
{
    juce::DatagramSocket motionSocket (false);
    ASSERT_TRUE (motionSocket.bindToPort (0, loopbackAddress().c_str()));
    const Endpoint motionAt { loopbackAddress().c_str(), motionSocket.getBoundPort() };

    VuRelayThread relay ({ "local", 0 }, [&] { return std::vector<Endpoint> { motionAt }; });
    ASSERT_TRUE (relay.bound());
    relay.start();

    juce::DatagramSocket sender (false);
    const auto sentBytes = bundle();
    ASSERT_EQ (sender.write (loopbackAddress().c_str(), relay.boundPort(), sentBytes.data(), static_cast<int> (sentBytes.size())),
               static_cast<int> (sentBytes.size()));

    ASSERT_EQ (motionSocket.waitUntilReady (true, 2000), 1);
    Datagram got (maxDatagram);
    const auto count = motionSocket.read (got.data(), static_cast<int> (got.size()), false);
    ASSERT_GT (count, 0);
    got.resize (static_cast<size_t> (count));
    EXPECT_EQ (got, sentBytes);
    relay.stop();
}

TEST (VuRelay, StopsAtOnceAndTwiceIsHarmless)
{
    VuRelayThread relay ({ "local", 0 }, [] { return std::vector<Endpoint> {}; });
    relay.start();
    const auto begun = std::chrono::steady_clock::now();
    relay.stop();
    relay.stop();
    EXPECT_LT (std::chrono::steady_clock::now() - begun, std::chrono::seconds (2));
}

TEST (VuRelay, ADestroyedRelayLeavesNoThread)
{
    for (int i = 0; i < 5; ++i)
    {
        VuRelayThread relay ({ "local", 0 }, [] { return std::vector<Endpoint> {}; });
        EXPECT_FALSE (relay.isRunning());
        EXPECT_TRUE (relay.start());
        EXPECT_TRUE (relay.isRunning());
        relay.stop();
        EXPECT_FALSE (relay.isRunning());
    }
    // Destroyed while running: the destructor must have joined (a Thread destroyed live would abort).
    {
        auto relay = std::make_unique<VuRelayThread> (Endpoint { "local", 0 }, [] { return std::vector<Endpoint> {}; });
        relay->start();
    }
    SUCCEED();
}

TEST (VuRelay, AFailedBindSaysSoAndStartsNothing)
{
    // An address that is not this machine's (TEST-NET-3): the bind is refused.
    VuRelayThread relay ({ "203.0.113.1", 0 }, [] { return std::vector<Endpoint> {}; });
    EXPECT_FALSE (relay.bound());
    EXPECT_FALSE (relay.start());
    EXPECT_FALSE (relay.isRunning());
}

TEST (VuRelay, TheAnalyzersEnvGetsTheLine)
{
    // Core renders the analyzer's conf.d file at its start, so the line reaches the analyzer
    // without a hand edit.
    const auto endpoint = b0::truth().endpoint ("core", "vu-relay");
    EXPECT_NE (analyzerBlock (b0::truth()).find ("OSC_VU_core=" + endpoint.host + ":" + std::to_string (endpoint.port) + "\n"),
               std::string::npos);
}
