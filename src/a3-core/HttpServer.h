// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Our own small HTTP server on juce::StreamingSocket: what the window of a3_core_web.py needs and
// nothing else. One thread per connection (cap: maxConnections, over it a 503), responses with
// Content-Length and `Connection: close`, and one server-sent-events mode. A reader that stops
// reading costs its own thread and nothing else: every write is non-blocking and gives up after
// a stall (see stallMillis), and no lock is held while writing.
//
// The rule the Python window lived under holds here too: it must never stop Core coming up. A
// refused bind is `false` and a journal line, never an exception.
#pragma once

#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

#include "Net.h"

namespace a3core
{
struct HttpRequest
{
    std::string method, path;
    std::map<std::string, std::string> headers;   // names in lower case
    std::string body;
};

struct HttpResponse
{
    int status = 200;
    std::string contentType = "text/plain";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;   // beyond Content-Type and Content-Length
};

class Routes
{
public:
    using Handler = std::function<HttpResponse (const HttpRequest&)>;
    // One call per event: the text after `data: `. Throwing ends that stream.
    using Events = std::function<std::string()>;
    // Made once per connection, so every reader has its own `previous`.
    using EventSource = std::function<Events()>;

    void get (const std::string& path, Handler handler);
    void post (const std::string& path, Handler handler);
    void stream (const std::string& path, EventSource source, int periodMillis);

    // What answers a path nobody registered; by default 404 `no such thing here`.
    void otherwise (Handler handler);

    struct Stream
    {
        EventSource source;
        int periodMillis;
    };

    const Handler* handlerFor (const std::string& method, const std::string& path) const;
    const Stream* streamFor (const std::string& path) const;
    HttpResponse unmatched (const HttpRequest& request) const;

private:
    std::map<std::pair<std::string, std::string>, Handler> handlers;
    std::map<std::string, Stream> streams;
    Handler fallback;
};

class HttpServer
{
public:
    // A write that makes no progress for this long drops the connection.
    static constexpr int stallMillis = 10000;
    // A client that has not finished its request by then is dropped.
    static constexpr int requestMillis = 10000;

    HttpServer (std::string bind, Routes routes, int maxConnections = 16);
    ~HttpServer();

    HttpServer (const HttpServer&) = delete;
    HttpServer& operator= (const HttpServer&) = delete;

    // Listen on `bind` ("host:port", the host an IPv4 address or a name, port 0 = the system chooses).
    // False, and `the window could not open on <bind>: <why>` in the journal (stdout, as Python's
    // print), for a bind with no colon, no host, a port that is not a number or out of range, or
    // an address that is taken.
    bool start (const Journal& journal);
    // How a connection gets its thread; the tests make it fail. Call before start().
    using Spawn = std::function<std::thread (std::function<void()>)>;
    void spawnWith (Spawn spawn) { makeThread = std::move (spawn); }
    // Joins every thread; open streams end. Safe to call twice.
    void stop();

    int port() const { return boundPort; }   // where it actually ended up
    int openConnections() const { return active.load(); }

private:
    struct Connection
    {
        std::thread thread;
        std::atomic<bool> done { false };
    };

    void acceptLoop();
    void serve (std::shared_ptr<juce::StreamingSocket> socket);
    void serveStream (int fd, const Routes::Stream& stream);
    void refuseOverTheCap (juce::StreamingSocket& socket);
    void reap();

    const std::string bind;
    const Routes routes;
    const int maxConnections;
    std::unique_ptr<juce::StreamingSocket> listener;
    std::thread acceptThread;
    std::list<std::unique_ptr<Connection>> connections;
    Journal say;
    Spawn makeThread = [] (std::function<void()> work) { return std::thread (std::move (work)); };
    bool saidThreadFailure = false;
    std::atomic<int> active { 0 };
    std::atomic<bool> stopping { false };
    int boundPort = -1;
};
}
