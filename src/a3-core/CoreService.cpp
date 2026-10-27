// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// B1's main block (a3-core.py:2173-2614), engine mode, line by line where it says something; the
// REAPER-only parts (feedback port, project saving, the replay thread) are not here (O8).
#include "CoreService.h"

#include <pthread.h>

#include <chrono>
#include <future>

#include "Announce.h"
#include "CoreApi.h"
#include "LatestWins.h"
#include "Render.h"
#include "StartupRecall.h"
#include "Subscribers.h"

namespace a3core
{
namespace
{
// B1's LATEST_WINS_LIMIT: what one pass takes from the main port at most.
constexpr size_t latestWinsLimit = 1024;

// How Python prints a socket address: ('<host>', <port>).
std::string pythonAddress (const Endpoint& endpoint) { return "('" + endpoint.host + "', " + std::to_string (endpoint.port) + ")"; }

std::string hostOf (const std::string& bind) { return bind.substr (0, bind.rfind (':')); }

void nameThisThread (const char* name) { pthread_setname_np (pthread_self(), name); }

std::string refusal (const std::string& why) { return "a3-core: " + why; }
}

CoreService::CoreService (ServiceOptions serviceOptions, Journal journalToUse, Truth loaded)
    : options (std::move (serviceOptions)), journal (std::move (journalToUse)), truth (std::move (loaded)),
      clock (options.clock != nullptr ? *options.clock : systemClock())
{
}

CoreService::~CoreService() { stop(); }

// a3_osc.load at B1's import, then the main block on the serve thread: B1's main thread did the
// start-up and then served, and everything Core does belongs to that one thread.
std::variant<std::unique_ptr<CoreService>, std::string> CoreService::start (ServiceOptions options, Journal journal)
{
    auto loaded = Truth::load (truthPath (options.truthPath, nullptr), options.networkPath);
    if (const auto* refused = std::get_if<TruthError> (&loaded))
        return refusal (refused->what);

    std::unique_ptr<CoreService> service (new CoreService (std::move (options), std::move (journal), std::move (std::get<Truth> (loaded))));
    std::promise<std::optional<std::string>> started;
    auto outcome = started.get_future();
    auto* const self = service.get();
    service->serveThread = std::thread ([self, started = std::move (started)]() mutable {
        nameThisThread ("a3-core-serve");
        std::optional<std::string> problem;
        try
        {
            problem = self->startUp();
        }
        catch (const TruthError& error)
        {
            problem = refusal (error.what);
        }
        catch (const std::exception& error)
        {
            problem = refusal (error.what());
        }
        const bool serve = ! problem;
        started.set_value (std::move (problem));
        if (serve)
            self->serve();
    });

    if (auto problem = outcome.get())
    {
        // B1 fell over here: what was said is said, nothing is flushed, every thread it began ends.
        service->serveThread.join();
        service->stopHelpers();
        return *problem;
    }
    return service;
}

std::optional<std::string> CoreService::startUp()
{
    say ("audio engine: engine");

    // The two sockets first, without a word: Core's replays need its main port's number (port 0 is
    // the system's pick) and the self socket's place before Core exists. B1 bound them later; a busy
    // main port is still said where B1 fell over.
    auto self = UdpSocket::bound (truth.host ("local"), 0);
    if (const auto* refused = std::get_if<OsError> (&self))
        return refusal ("cannot bind the socket of Core's own replays: " + refused->text);
    selfSocket = std::move (std::get<UdpSocket> (self));
    selfBuffer = selfSocket.widenReceiveBuffer();
    auto main = UdpSocket::bound (options.core.ip, options.core.port);
    if (auto* bound = std::get_if<UdpSocket> (&main))
    {
        mainSocket = std::move (*bound);
        mainBuffer = mainSocket->widenReceiveBuffer();
        options.core.port = mainSocket->local().port;
    }
    else
        mainRefused = std::get<OsError> (main);

    net = std::make_unique<UdpNet> (selfSocket);
    core = std::make_unique<Core> (CoreEnvironment { truth, *net, clock, journal, options.stateDir, newBootId,
                                                     [] { return randomToken(); }, selfSocket.local() },
                                   options.core);
    try
    {
        core->startPeers();
    }
    catch (const SubscriberError& problem)
    {
        return refusal (problem.what());   // sys.exit(f"a3-core: {problem}")
    }

    bindTheMetersInbox();
    core->startSpeaking();
    if (! options.noWeb)
        openTheWindow();

    if (mainRefused)
        return refusal ("cannot listen on " + options.core.ip + ":" + std::to_string (options.core.port) + ": " + mainRefused->text);
    say ("Serving on " + pythonAddress (mainSocket->local()));

    if (truth.networkProblem)
        say ("network.json refused, the package's defaults are used: " + *truth.networkProblem, true);
    if (truth.metersProblem)
        say ("network.json's meters refused, the package's are used: " + *truth.metersProblem, true);
    try
    {
        say ("aes67: " + writeUserFiles (truth, options.home, options.hostname, options.packageShare.getChildFile ("pipewire/pipewire-aes67.conf.in")));
    }
    catch (const RenderError& problem)
    {
        say ("could not render osc.env / the analyzer's conf.d file / the AES67 config: " + problem.what, true);
    }
    startAnnouncing();

    serving = true;
    return std::nullopt;
}

// The analyzer's and StemDeck's meters, for a remote Motion and the desks. A busy port costs a remote
// Motion its meters, not the rig its sound. Read by the serve loop, not a thread of its own (P8).
void CoreService::bindTheMetersInbox()
{
    auto inbox = UdpSocket::bound (truth.endpoint ("core", "vu-relay").host, truth.port ("core", "vu-relay"));
    if (const auto* refused = std::get_if<OsError> (&inbox))
    {
        say ("no meters for a remote Motion or the desks: " + refused->text, true);
        return;
    }
    vuSocket = std::move (std::get<UdpSocket> (inbox));
    say ("forwarding the analyzer's meters from " + pythonAddress (vuSocket->local()) + " to a remote Motion and every desk followed");
}

// The window, if it will come. Its failure is not Core's: HttpServer says it, and the rig still
// makes sound. Only the monitor's routes; status, send and the rig are Task 21's.
void CoreService::openTheWindow()
{
    const auto bind = options.webBind.empty() ? defaultBind (truth) : options.webBind;
    Routes routes;
    addMonitorRoutes (routes, MonitorSources { &truth, core->traffic(), &core->devices(),
                                               options.packageShare.getChildFile ("osc-register.json"), clock });
    auto server = std::make_unique<HttpServer> (bind, std::move (routes));
    if (! server->start (journal))
        return;
    windowHost = hostOf (bind);
    say ("window on http://" + windowHost + ":" + std::to_string (server->port()));
    window = std::move (server);
}

// /core/here every 2 s by broadcast, from a thread of its own (B1's "a3-announce").
void CoreService::startAnnouncing()
{
    announceSocket.allowBroadcast();
    const Endpoint target { broadcastAddress (truth.network()), truth.port ("devices", "announce") };
    const auto packet = announcement (truth, newBootId());
    announcing = true;
    announceThread = std::thread ([this, target, packet] {
        nameThisThread ("a3-announce");
        announce ([this] (const Endpoint& to, const std::vector<uint8_t>& data) { announceSocket.sendTo (to, data); },
                  target, packet, everySeconds,
                  [this] (double seconds) {
                      std::unique_lock hold (announceLock);
                      announceWake.wait_for (hold, std::chrono::duration<double> (seconds), [this] { return ! announcing; });
                  },
                  [this] {
                      const std::scoped_lock hold (announceLock);
                      return announcing;
                  },
                  [this] (const std::string& text) { say (text, true); });
    });
    say ("announcing the truth to " + target.host + ":" + std::to_string (target.port));
}

// a3_core_latest.serve: the main port with the newest position winning, the meters' inbox, a tick
// after every pass and at the latest every 0.1 s -- one thread. Then the SIGTERM handler's flush,
// here too.
void CoreService::serve()
{
    ServeLoop loop { mainSocket->fd(),
                     [this] (const std::vector<uint8_t>& data, const Endpoint& from) { core->handlePacket (from, data.data(), data.size()); },
                     [this] (const std::vector<uint8_t>& data) {
                         return positionKey (data.data(), data.size(), [this] (const std::string& address) { return isPosition (truth, address); });
                     },
                     latestWinsLimit,
                     [this] (const std::string& text) { say (text, true); },
                     [this] { return serving.load(); },
                     [this] { core->tick(); } };
    if (vuSocket)
        loop.also.push_back ({ vuSocket->fd(), [this] (const std::vector<uint8_t>& data, const Endpoint& from) {
                                  core->handleVu (from, data.data(), data.size());
                              } });
    a3core::serve (loop);
    core->stop();
}

void CoreService::stop()
{
    serving = false;
    if (serveThread.joinable())
        serveThread.join();
    stopHelpers();
}

void CoreService::stopHelpers()
{
    if (window)
        window->stop();
    {
        const std::scoped_lock hold (announceLock);
        announcing = false;
    }
    announceWake.notify_all();
    if (announceThread.joinable())
        announceThread.join();
}

Endpoint CoreService::mainPort() const { return mainSocket ? mainSocket->local() : Endpoint {}; }
Endpoint CoreService::vuRelayPort() const { return vuSocket ? vuSocket->local() : Endpoint {}; }
Endpoint CoreService::webPort() const { return window ? Endpoint { windowHost, window->port() } : Endpoint {}; }
}
