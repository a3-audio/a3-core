// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HttpServer.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <exception>
#include <optional>

namespace a3core
{
namespace
{
constexpr int pollSliceMillis = 100;
constexpr size_t maxHeaderBytes = 16 * 1024;
constexpr size_t maxBodyBytes = 1024 * 1024;
// The accept thread does the refusing, so this is the longest any one refusal can hold the door.
constexpr int refuseDrainMillis = 20;

using Steady = std::chrono::steady_clock;

bool past (Steady::time_point deadline) { return Steady::now() >= deadline; }

const char* reasonOf (int status)
{
    switch (status)
    {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "Status";
    }
}

HttpResponse plain (int status, const std::string& text)
{
    HttpResponse response;
    response.status = status;
    response.body = text;
    return response;
}

std::string head (const HttpResponse& response)
{
    std::string text = "HTTP/1.1 " + std::to_string (response.status) + " " + reasonOf (response.status) + "\r\n";
    text += "Content-Type: " + response.contentType + "\r\n";
    text += "Content-Length: " + std::to_string (response.body.size()) + "\r\n";
    for (const auto& header : response.headers)
        text += header.first + ": " + header.second + "\r\n";
    return text + "Connection: close\r\n\r\n";
}

std::string lowered (std::string text)
{
    std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return text;
}

std::string trimmed (const std::string& text)
{
    const auto first = text.find_first_not_of (" \t");
    if (first == std::string::npos)
        return {};
    return text.substr (first, text.find_last_not_of (" \t") - first + 1);
}

// Writes without ever blocking in the kernel: a reader that stopped reading leaves send() with
// EAGAIN, and this waits in slices that can be cut short by `stopping` or by the stall limit.
bool writeAll (int fd, const std::string& data, const std::atomic<bool>& stopping)
{
    size_t done = 0;
    auto lastProgress = Steady::now();
    while (done < data.size())
    {
        if (stopping)
            return false;
        const auto n = ::send (fd, data.data() + done, data.size() - done, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0)
        {
            done += static_cast<size_t> (n);
            lastProgress = Steady::now();
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if (past (lastProgress + std::chrono::milliseconds (HttpServer::stallMillis)))
                return false;
            pollfd waiting { fd, POLLOUT, 0 };
            ::poll (&waiting, 1, pollSliceMillis);
            continue;
        }
        return false;
    }
    return true;
}

// Close the way a client can still read what was sent: say we are done writing, then let the client's
// own close (or a short while) pass before the descriptor goes. Closing with unread request bytes
// would reset the connection and could take the response with it.
void finish (int fd, int waitMillis)
{
    ::shutdown (fd, SHUT_WR);
    const auto end = Steady::now() + std::chrono::milliseconds (waitMillis);
    char scrap[1024];
    while (! past (end))
    {
        pollfd waiting { fd, POLLIN, 0 };
        if (::poll (&waiting, 1, 20) <= 0)
            continue;
        if (::recv (fd, scrap, sizeof (scrap), MSG_DONTWAIT) == 0)
            return;
    }
}

struct Parsed
{
    std::optional<HttpRequest> request;
    int refusal = 0;
    std::string why;
};

// Reads one request: the head up to a blank line, then Content-Length bytes.
Parsed readRequest (int fd, const std::atomic<bool>& stopping)
{
    Parsed parsed;
    std::string data;
    const auto deadline = Steady::now() + std::chrono::milliseconds (HttpServer::requestMillis);
    size_t headEnd = std::string::npos;
    size_t bodyWanted = 0;
    HttpRequest request;
    char buffer[8192];

    for (;;)
    {
        if (headEnd == std::string::npos)
        {
            headEnd = data.find ("\r\n\r\n");
            if (headEnd == std::string::npos && data.size() > maxHeaderBytes)
            {
                parsed.refusal = 431;
                parsed.why = "the request head is too long";
                return parsed;
            }
            if (headEnd != std::string::npos)
            {
                const auto lineEnd = data.find ("\r\n");
                const auto line = data.substr (0, lineEnd);
                const auto firstSpace = line.find (' ');
                const auto secondSpace = firstSpace == std::string::npos ? std::string::npos : line.find (' ', firstSpace + 1);
                if (firstSpace == std::string::npos || secondSpace == std::string::npos || line.compare (secondSpace + 1, 5, "HTTP/") != 0)
                {
                    parsed.refusal = 400;
                    parsed.why = "that is not an HTTP request";
                    return parsed;
                }
                request.method = line.substr (0, firstSpace);
                request.path = line.substr (firstSpace + 1, secondSpace - firstSpace - 1);

                for (auto at = lineEnd; at < headEnd;)
                {
                    const auto next = data.find ("\r\n", at + 2);
                    const auto field = data.substr (at + 2, next - at - 2);
                    const auto colon = field.find (':');
                    if (colon != std::string::npos)
                        request.headers[lowered (trimmed (field.substr (0, colon)))] = trimmed (field.substr (colon + 1));
                    at = next;
                }
                const auto length = request.headers.find ("content-length");
                if (length != request.headers.end())
                {
                    const auto& digits = length->second;
                    if (digits.empty() || digits.size() > 9 || digits.find_first_not_of ("0123456789") != std::string::npos)
                    {
                        parsed.refusal = 400;
                        parsed.why = "Content-Length is not a number";
                        return parsed;
                    }
                    bodyWanted = static_cast<size_t> (std::stoul (digits));
                    if (bodyWanted > maxBodyBytes)
                    {
                        parsed.refusal = 413;
                        parsed.why = "the body is too long";
                        return parsed;
                    }
                }
            }
        }
        if (headEnd != std::string::npos && data.size() >= headEnd + 4 + bodyWanted)
        {
            request.body = data.substr (headEnd + 4, bodyWanted);
            parsed.request = std::move (request);
            return parsed;
        }

        if (stopping || past (deadline))
            return parsed;
        pollfd waiting { fd, POLLIN, 0 };
        if (::poll (&waiting, 1, pollSliceMillis) <= 0)
            continue;
        const auto n = ::recv (fd, buffer, sizeof (buffer), MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR))
            return parsed;
        if (n > 0)
            data.append (buffer, static_cast<size_t> (n));
    }
}

struct Bind
{
    std::string host;
    int port = 0;
    std::string why;   // empty when the bind is fine
};

// "host:port", strictly: the colon is required and so is the host. An empty host means every interface,
// which is the one thing the loopback default exists to prevent, so a value without one is refused
// and not guessed at.
Bind parseBind (const std::string& bind)
{
    Bind parsed;
    const auto colon = bind.rfind (':');
    if (colon == std::string::npos)
    {
        parsed.why = "the bind wants host:port, got '" + bind + "'";
        return parsed;
    }
    parsed.host = bind.substr (0, colon);
    if (parsed.host.empty())
    {
        parsed.why = "the bind wants a host, got '" + bind + "' -- write the loopback address and the port, or the all-interfaces address if every interface is really what you mean";
        return parsed;
    }
    auto digits = trimmed (bind.substr (colon + 1));
    const bool negative = ! digits.empty() && digits.front() == '-';
    if (! digits.empty() && (digits.front() == '-' || digits.front() == '+'))
        digits.erase (0, 1);
    if (digits.empty() || digits.find_first_not_of ("0123456789") != std::string::npos)
    {
        parsed.why = "the port is not a number: '" + bind.substr (colon + 1) + "'";
        return parsed;
    }
    if (negative || digits.size() > 5 || std::stoi (digits) > 65535)
    {
        parsed.why = "the port is out of range: " + std::string (negative ? "-" : "") + digits;
        return parsed;
    }
    parsed.port = std::stoi (digits);
    return parsed;
}

// The dotted address a host name stands for (IPv4: JUCE's listener binds an address, not a name).
std::string addressOf (const std::string& host)
{
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (::getaddrinfo (host.c_str(), nullptr, &hints, &found) != 0 || found == nullptr)
        return {};
    char text[INET_ADDRSTRLEN] = {};
    const auto* address = reinterpret_cast<const sockaddr_in*> (found->ai_addr);
    ::inet_ntop (AF_INET, &address->sin_addr, text, sizeof (text));
    ::freeaddrinfo (found);
    return text;
}
}

// Routes

void Routes::get (const std::string& path, Handler handler) { handlers[{ "GET", path }] = std::move (handler); }
void Routes::post (const std::string& path, Handler handler) { handlers[{ "POST", path }] = std::move (handler); }

void Routes::stream (const std::string& path, EventSource source, int periodMillis)
{
    streams[path] = { std::move (source), periodMillis };
}

void Routes::otherwise (Handler handler) { fallback = std::move (handler); }

const Routes::Handler* Routes::handlerFor (const std::string& method, const std::string& path) const
{
    const auto found = handlers.find ({ method, path });
    return found == handlers.end() ? nullptr : &found->second;
}

const Routes::Stream* Routes::streamFor (const std::string& path) const
{
    const auto found = streams.find (path);
    return found == streams.end() ? nullptr : &found->second;
}

HttpResponse Routes::unmatched (const HttpRequest& request) const
{
    return fallback ? fallback (request) : plain (404, "no such thing here");
}

// HttpServer

HttpServer::HttpServer (std::string bindIn, Routes routesIn, int maxIn)
    : bind (std::move (bindIn)), routes (std::move (routesIn)), maxConnections (maxIn) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start (const Journal& journal)
{
    auto refuse = [&] (const std::string& why) {
        journal ("the window could not open on " + bind + ": " + why, false);
        return false;
    };

    if (listener != nullptr)
        return refuse ("it is already open");
    const auto wanted = parseBind (bind);
    if (! wanted.why.empty())
        return refuse (wanted.why);
    const auto address = addressOf (wanted.host);
    if (address.empty())
        return refuse ("'" + wanted.host + "' is not an IPv4 address or a name that has one");

    auto socket = std::make_unique<juce::StreamingSocket>();
    if (! socket->createListener (wanted.port, address))
        return refuse ("the address is already in use, or not one of this machine's");

    say = journal;
    saidThreadFailure = false;
    boundPort = socket->getBoundPort();
    listener = std::move (socket);
    stopping = false;
    acceptThread = std::thread ([this] { acceptLoop(); });
    return true;
}

void HttpServer::stop()
{
    stopping = true;
    if (acceptThread.joinable())
        acceptThread.join();
    for (auto& connection : connections)
        if (connection->thread.joinable())
            connection->thread.join();
    connections.clear();
    listener.reset();
}

void HttpServer::reap()
{
    for (auto at = connections.begin(); at != connections.end();)
    {
        if ((*at)->done)
        {
            (*at)->thread.join();
            at = connections.erase (at);
        }
        else
            ++at;
    }
}

void HttpServer::acceptLoop()
{
    const int listening = listener->getRawSocketHandle();
    while (! stopping)
    {
        reap();
        pollfd waiting { listening, POLLIN, 0 };
        if (::poll (&waiting, 1, pollSliceMillis) <= 0)
            continue;
        std::unique_ptr<juce::StreamingSocket> socket (listener->waitForNextConnection());
        if (socket == nullptr)
            continue;

        if (active >= maxConnections)
        {
            refuseOverTheCap (*socket);
            continue;
        }
        // Shared, so that a thread that cannot be made leaves the socket here to be closed.
        std::shared_ptr<juce::StreamingSocket> shared (std::move (socket));
        ++active;
        auto connection = std::make_unique<Connection>();
        auto* raw = connection.get();
        try
        {
            connection->thread = makeThread ([this, raw, shared] {
                serve (shared);
                --active;
                raw->done = true;
            });
        }
        catch (const std::exception& problem)
        {
            // Out of threads or memory: this connection is lost, the window is not.
            --active;
            if (! saidThreadFailure)
            {
                saidThreadFailure = true;
                say ("the window could not start a thread for a connection: " + std::string (problem.what()), false);
            }
            continue;
        }
        connections.push_back (std::move (connection));
    }
}

void HttpServer::refuseOverTheCap (juce::StreamingSocket& socket)
{
    const int fd = socket.getRawSocketHandle();
    char arrived[1024];
    ::recv (fd, arrived, sizeof (arrived), MSG_DONTWAIT);   // what the client already sent need not meet a reset
    auto response = plain (503, "too many connections");
    writeAll (fd, head (response) + response.body, stopping);
    finish (fd, refuseDrainMillis);
}

void HttpServer::serveStream (int fd, const Routes::Stream& stream)
{
    Routes::Events next;
    try
    {
        next = stream.source();
    }
    catch (...)
    {
        writeAll (fd, head (plain (500, "the stream could not start")) + "the stream could not start", stopping);
        finish (fd, 200);
        return;
    }

    static const std::string streamHead = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
    if (! writeAll (fd, streamHead, stopping))
        return;

    while (! stopping)
    {
        std::string data;
        try
        {
            data = next();
        }
        catch (...)
        {
            finish (fd, 200);   // the source has nothing more to say; what was sent is still read
            return;
        }
        if (! writeAll (fd, "data: " + data + "\n\n", stopping))
            return;

        // Wait for the next tick, and notice a client that hung up meanwhile: a closed tab is dropped
        // in silence and at once, not at the next write.
        const auto until = Steady::now() + std::chrono::milliseconds (stream.periodMillis);
        while (! stopping && ! past (until))
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds> (until - Steady::now()).count();
            pollfd waiting { fd, POLLIN, 0 };
            if (::poll (&waiting, 1, static_cast<int> (std::clamp<long long> (left, 1, pollSliceMillis))) <= 0)
                continue;
            char scrap[256];
            const auto n = ::recv (fd, scrap, sizeof (scrap), MSG_DONTWAIT);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR))
                return;
        }
    }
}

void HttpServer::serve (std::shared_ptr<juce::StreamingSocket> socket)
{
    const int fd = socket->getRawSocketHandle();
    const auto reply = [&] (const HttpResponse& response) {
        writeAll (fd, head (response) + response.body, stopping);
        finish (fd, 200);
    };

    const auto parsed = readRequest (fd, stopping);
    if (! parsed.request)
    {
        if (parsed.refusal != 0)
            reply (plain (parsed.refusal, parsed.why));
        return;
    }
    const auto& request = *parsed.request;

    if (request.method != "GET" && request.method != "POST")
        return reply (plain (501, "this window answers GET and POST"));

    if (request.method == "GET")
    {
        if (const auto* stream = routes.streamFor (request.path))
        {
            serveStream (fd, *stream);
            return;
        }
    }

    HttpResponse response;
    try
    {
        const auto* handler = routes.handlerFor (request.method, request.path);
        response = handler != nullptr ? (*handler) (request) : routes.unmatched (request);
    }
    catch (const std::exception& problem)
    {
        response = plain (500, problem.what());
    }
    catch (...)
    {
        response = plain (500, "the handler failed");
    }
    reply (response);
}
}
