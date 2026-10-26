// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The HTTP server of the window (src/a3-core/HttpServer.h) on its own: real HTTP to an ephemeral port on
// loopback, every server stopped at the end of its test. The cases of a3-core's test_core_web.py that
// are about binding (a bad bind and a busy port must not take Core down) are in CoreApi.cpp; here is
// what the server does for any route.
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "HttpClient.h"
#include "HttpServer.h"

using namespace a3core;
namespace hc = httpclient;

namespace
{
struct Said
{
    std::mutex lock;
    std::vector<std::pair<std::string, bool>> lines;

    Journal journal()
    {
        return [this] (const std::string& line, bool toStderr) {
            std::lock_guard<std::mutex> guard (lock);
            lines.emplace_back (line, toStderr);
        };
    }
};

std::string loopback() { return loopbackAddress(); }

Routes sampleRoutes()
{
    Routes routes;
    routes.get ("/hello", [] (const HttpRequest&) {
        HttpResponse response;
        response.contentType = "application/json";
        response.body = "{\"hello\":1}";
        response.headers.emplace_back ("X-Sample", "yes");
        return response;
    });
    routes.get ("/boom", [] (const HttpRequest&) -> HttpResponse { throw std::runtime_error ("this page is broken"); });
    routes.post ("/echo", [] (const HttpRequest& request) {
        HttpResponse response;
        response.body = request.method + ":" + request.body;
        return response;
    });
    return routes;
}

double secondsSince (std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
}

// Waits for a condition the server meets on its own threads.
template <typename Condition>
bool eventually (Condition condition, int millis = 3000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds (millis);
    while (std::chrono::steady_clock::now() < end)
    {
        if (condition())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return condition();
}
}

TEST (HttpServer, AGetAnswersWithItsLengthAndHeaders)
{
    Said said;
    HttpServer server (loopback() + ":0", sampleRoutes());
    ASSERT_TRUE (server.start (said.journal()));
    ASSERT_GT (server.port(), 0);

    const auto reply = hc::get (server.port(), "/hello");
    EXPECT_EQ (reply.status, 200);
    EXPECT_EQ (reply.body, "{\"hello\":1}");
    EXPECT_EQ (reply.headers.at ("content-type"), "application/json");
    EXPECT_EQ (reply.headers.at ("content-length"), "11");
    EXPECT_EQ (reply.headers.at ("x-sample"), "yes");
    EXPECT_TRUE (said.lines.empty());
}

TEST (HttpServer, APathNobodyRegisteredIsA404AndNotACrash)
{
    Said said;
    HttpServer server (loopback() + ":0", sampleRoutes());
    ASSERT_TRUE (server.start (said.journal()));
    const auto reply = hc::get (server.port(), "/nope");
    EXPECT_EQ (reply.status, 404);
    EXPECT_EQ (reply.body, "no such thing here");
    EXPECT_EQ (reply.headers.at ("content-type"), "text/plain");
    // A route's path under the wrong method is just as absent.
    EXPECT_EQ (hc::post (server.port(), "/hello", "x").status, 404);
    EXPECT_EQ (hc::get (server.port(), "/echo").status, 404);
}

TEST (HttpServer, ABrokenHandlerAnswers500AndTheServerGoesOn)
{
    Said said;
    HttpServer server (loopback() + ":0", sampleRoutes());
    ASSERT_TRUE (server.start (said.journal()));
    const auto broken = hc::get (server.port(), "/boom");
    EXPECT_EQ (broken.status, 500);
    EXPECT_EQ (broken.body, "this page is broken");
    EXPECT_EQ (hc::get (server.port(), "/hello").status, 200);
}

TEST (HttpServer, APostBodyIsReadByItsLength)
{
    Said said;
    HttpServer server (loopback() + ":0", sampleRoutes());
    ASSERT_TRUE (server.start (said.journal()));
    const std::string body (5000, 'x');
    const auto reply = hc::post (server.port(), "/echo", body);
    EXPECT_EQ (reply.status, 200);
    EXPECT_EQ (reply.body, "POST:" + body);
}

TEST (HttpServer, ARequestThatIsNotHttpIsAskedAgainInWords)
{
    Said said;
    HttpServer server (loopback() + ":0", sampleRoutes());
    ASSERT_TRUE (server.start (said.journal()));
    const auto reply = hc::request (server.port(), "this is not http\r\n\r\n");
    EXPECT_EQ (reply.status, 400);
    EXPECT_FALSE (reply.body.empty());
    EXPECT_EQ (hc::get (server.port(), "/hello").status, 200);
}

TEST (HttpServer, AMethodItDoesNotKnowIsRefused)
{
    Said said;
    HttpServer server (loopback() + ":0", sampleRoutes());
    ASSERT_TRUE (server.start (said.journal()));
    EXPECT_EQ (hc::request (server.port(), "DELETE /hello HTTP/1.0\r\n\r\n").status, 501);
}

TEST (HttpServer, AHeaderThatNeverEndsIsRefused)
{
    Said said;
    HttpServer server (loopback() + ":0", sampleRoutes());
    ASSERT_TRUE (server.start (said.journal()));
    const auto reply = hc::request (server.port(), "GET /hello HTTP/1.0\r\nX-Long: " + std::string (40000, 'a'));
    EXPECT_EQ (reply.status, 431);
}

// Binding. The Python window returned False for each of these and printed one line; Core went on.

TEST (HttpServer, BindWithoutHostIsRefused)
{
    for (const std::string bind : { std::string (":39080"), std::string ("39080"), std::string ("") })
    {
        Said said;
        HttpServer server (bind, sampleRoutes());
        EXPECT_FALSE (server.start (said.journal())) << bind;
        ASSERT_EQ (said.lines.size(), 1u) << bind;
        EXPECT_EQ (said.lines[0].first.rfind ("the window could not open on " + bind + ": ", 0), 0u) << said.lines[0].first;
        EXPECT_FALSE (said.lines[0].second) << "a line for stdout, as Python's print";
        EXPECT_FALSE (hc::connects (39080)) << "nothing may have been bound for " << bind;
    }
}

TEST (HttpServer, ABindNobodyCanReadIsALineToo)
{
    for (const std::string bind : { loopback() + ":not-a-port", loopback() + ":99080", loopback() + ":-1", loopback() + ":" })
    {
        Said said;
        HttpServer server (bind, sampleRoutes());
        EXPECT_FALSE (server.start (said.journal())) << bind;
        ASSERT_EQ (said.lines.size(), 1u) << bind;
        EXPECT_NE (said.lines[0].first.find ("the window could not open on " + bind + ": "), std::string::npos);
    }
}

TEST (HttpServer, BusyPortIsALine)
{
    Said said;
    HttpServer first (loopback() + ":0", sampleRoutes());
    ASSERT_TRUE (first.start (said.journal()));

    HttpServer second (loopback() + ":" + std::to_string (first.port()), sampleRoutes());
    EXPECT_FALSE (second.start (said.journal()));
    ASSERT_EQ (said.lines.size(), 1u);
    EXPECT_EQ (said.lines[0].first.rfind ("the window could not open on " + loopback() + ":" + std::to_string (first.port()) + ": ", 0), 0u)
        << said.lines[0].first;
    EXPECT_EQ (hc::get (first.port(), "/hello").status, 200) << "the one that holds the port is not disturbed";
}

TEST (HttpServer, AHostThatIsNotAnAddressOfThisMachineIsALine)
{
    Said said;
    HttpServer server ("no-such-host.invalid:0", sampleRoutes());
    EXPECT_FALSE (server.start (said.journal()));
    EXPECT_EQ (said.lines.size(), 1u);
}

// The cap.

TEST (HttpServer, OverTheCapIs503)
{
    Said said;
    HttpServer server (loopback() + ":0", sampleRoutes(), 3);
    ASSERT_TRUE (server.start (said.journal()));

    // Three that say nothing hold the three places.
    int held[3];
    for (auto& fd : held)
        fd = hc::connectTo (server.port());
    ASSERT_TRUE (eventually ([&] { return server.openConnections() == 3; }));

    const auto refused = hc::get (server.port(), "/hello");
    EXPECT_EQ (refused.status, 503);
    EXPECT_EQ (refused.body, "too many connections");

    // One goes, a place is free again.
    ::close (held[0]);
    ASSERT_TRUE (eventually ([&] { return server.openConnections() == 2; }));
    EXPECT_EQ (hc::get (server.port(), "/hello").status, 200);

    for (int i = 1; i < 3; ++i)
        ::close (held[i]);
}

TEST (HttpServer, TheDefaultCapIsSixteen)
{
    Said said;
    HttpServer server (loopback() + ":0", sampleRoutes());
    ASSERT_TRUE (server.start (said.journal()));
    std::vector<int> held;
    for (int i = 0; i < 16; ++i)
        held.push_back (hc::connectTo (server.port()));
    ASSERT_TRUE (eventually ([&] { return server.openConnections() == 16; }));
    EXPECT_EQ (hc::get (server.port(), "/hello").status, 503);
    for (const auto fd : held)
        ::close (fd);
}

// The stream.

TEST (HttpServer, AStreamSendsAnEventEveryPeriodAndEachReaderHasItsOwn)
{
    Said said;
    Routes routes;
    routes.stream ("/ticks", [] {
        auto count = std::make_shared<int> (0);
        return [count] { return "tick " + std::to_string ((*count)++); };
    }, 50);
    HttpServer server (loopback() + ":0", std::move (routes));
    ASSERT_TRUE (server.start (said.journal()));

    const auto start = std::chrono::steady_clock::now();
    const auto one = hc::events (server.port(), "/ticks", 5);
    ASSERT_EQ (one.size(), 5u);
    EXPECT_EQ (one[0], "data: tick 0");
    EXPECT_EQ (one[4], "data: tick 4");
    EXPECT_GE (secondsSince (start), 0.18) << "five events at 50 ms are not instant";
    EXPECT_LT (secondsSince (start), 2.0);

    const auto two = hc::events (server.port(), "/ticks", 2);
    ASSERT_EQ (two.size(), 2u);
    EXPECT_EQ (two[0], "data: tick 0") << "a second reader starts from its own beginning";
}

TEST (HttpServer, AStreamAnswersWithTheHeadersOfAnEventStream)
{
    Said said;
    Routes routes;
    routes.stream ("/ticks", [] { return [] { return std::string ("x"); }; }, 20);
    HttpServer server (loopback() + ":0", std::move (routes));
    ASSERT_TRUE (server.start (said.journal()));
    const int fd = hc::connectTo (server.port());
    hc::sendAll (fd, "GET /ticks HTTP/1.0\r\n\r\n");
    char buffer[512];
    pollfd p { fd, POLLIN, 0 };
    ASSERT_GT (::poll (&p, 1, 3000), 0);
    const auto n = ::recv (fd, buffer, sizeof (buffer), 0);
    ::close (fd);
    ASSERT_GT (n, 0);
    const auto reply = hc::parse (std::string (buffer, static_cast<size_t> (n)));
    EXPECT_EQ (reply.status, 200);
    EXPECT_EQ (reply.headers.at ("content-type"), "text/event-stream");
    EXPECT_EQ (reply.headers.at ("cache-control"), "no-cache");
}

TEST (HttpServer, AReaderThatHangsUpFreesItsPlace)
{
    Said said;
    Routes routes;
    routes.stream ("/ticks", [] { return [] { return std::string ("x"); }; }, 20);
    HttpServer server (loopback() + ":0", std::move (routes));
    ASSERT_TRUE (server.start (said.journal()));
    ASSERT_EQ (hc::events (server.port(), "/ticks", 2).size(), 2u);
    EXPECT_TRUE (eventually ([&] { return server.openConnections() == 0; })) << "a closed tab is dropped in silence";
    EXPECT_TRUE (said.lines.empty());
}

TEST (HttpServer, AStreamThatThrowsEndsAndNothingElse)
{
    Said said;
    Routes routes;
    routes.stream ("/short", [] {
        auto count = std::make_shared<int> (0);
        return [count]() -> std::string {
            if ((*count)++ == 1)
                throw std::runtime_error ("done");
            return "once";
        };
    }, 20);
    routes.get ("/hello", [] (const HttpRequest&) { return HttpResponse {}; });
    HttpServer server (loopback() + ":0", std::move (routes));
    ASSERT_TRUE (server.start (said.journal()));
    EXPECT_EQ (hc::events (server.port(), "/short", 3, 2000).size(), 1u);
    EXPECT_EQ (hc::get (server.port(), "/hello").status, 200);
}

// Shutdown.

TEST (HttpServer, StopEndsEveryThreadAndTheListener)
{
    Said said;
    Routes routes = sampleRoutes();
    routes.stream ("/ticks", [] { return [] { return std::string ("x"); }; }, 20);
    HttpServer server (loopback() + ":0", std::move (routes));
    ASSERT_TRUE (server.start (said.journal()));
    const int port = server.port();

    const int silent = hc::connectTo (port);
    const int streaming = hc::connectTo (port);
    hc::sendAll (streaming, "GET /ticks HTTP/1.0\r\n\r\n");
    ASSERT_TRUE (eventually ([&] { return server.openConnections() == 2; }));

    const auto start = std::chrono::steady_clock::now();
    server.stop();
    EXPECT_LT (secondsSince (start), 1.0) << "stop must not wait for a client";
    EXPECT_EQ (server.openConnections(), 0);
    EXPECT_FALSE (hc::connects (port));
    ::close (silent);
    ::close (streaming);
    server.stop();   // twice is fine
}

TEST (HttpServer, ADestroyedServerLeavesTheNextOneItsPort)
{
    Said said;
    int port = 0;
    {
        HttpServer server (loopback() + ":0", sampleRoutes());
        ASSERT_TRUE (server.start (said.journal()));
        port = server.port();
        EXPECT_EQ (hc::get (port, "/hello").status, 200);
    }
    HttpServer next (loopback() + ":" + std::to_string (port), sampleRoutes());
    EXPECT_TRUE (next.start (said.journal())) << "the port is free the moment the destructor returns";
}
