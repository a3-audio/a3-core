// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The Core on real sockets: B1's a3-core.py main block in engine mode (a3-core 7a7b2441f74e). The
// whole start-up and every packet, meter datagram, tick and debounce flush after it run on ONE
// thread, the serve thread (P8) -- B1's main thread, which started everything and then served.
// Besides it only the announcer (/core/here every 2 s) and the window's HTTP threads run; the
// window reads Core's tables through their own mutexes.
//
// Where things are (home, the state directory, the package's share, the host name, the truth) is
// given, never looked up here: the platform seam fills these in (Task 23).
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>

#include <juce_core/juce_core.h>

#include "Core.h"
#include "HttpServer.h"
#include "Net.h"
#include "Truth.h"
#include "UdpNet.h"

namespace a3core
{
struct ServiceOptions
{
    CoreArgs core;              // core.port 0: the system picks Core's main port
    std::string webBind;        // "host:port"; empty: the truth's core.web (default_bind)
    bool noWeb = false;
    juce::File home, stateDir, packageShare;   // packageShare: B1's ~/.local/share/a3-core
    std::string hostname;
    std::optional<juce::File> truthPath;       // none: the installed a3-osc.json
    std::optional<juce::File> networkPath;     // the maintainer's network.json; none: no file
    Clock* clock = nullptr;                    // the one clock of every `now`; none: the system's
};

class CoreService
{
public:
    // The start-up, on the serve thread; the service once the main port is served, or why Core cannot
    // run ("a3-core: <what>: <why>", for the app to print before exit 1).
    static std::variant<std::unique_ptr<CoreService>, std::string> start (ServiceOptions, Journal);

    ~CoreService();
    CoreService (const CoreService&) = delete;
    CoreService& operator= (const CoreService&) = delete;

    // The SIGTERM handler's stop(): state, evening and the address list go to disk (on the serve
    // thread), then every thread this service started is joined. Harmless to call twice.
    void stop();

    // Waits up to `seconds` for the serve loop to end: nullopt while it still serves; "" once stop()
    // ended it; the reason line when it ended on its own (a socket it can no longer wait on) -- Core is
    // gone then, and the app exits non-zero so that systemd starts it again.
    std::optional<std::string> waitUntilFinished (double seconds);

    Endpoint mainPort() const;
    Endpoint vuRelayPort() const;   // port 0: the inbox could not be bound
    Endpoint webPort() const;       // port 0: no window
    // What the kernel granted the main and the self socket (a3_core_receive_buffer).
    std::pair<int, int> receiveBuffers() const { return { mainBuffer, selfBuffer }; }

private:
    CoreService (ServiceOptions, Journal, Truth);

    std::optional<std::string> startUp();   // on the serve thread; an error ends the start
    void bindTheMetersInbox();
    void openTheWindow();
    void startAnnouncing();
    void serve();                           // the serve loop, then Core's stop(), on the serve thread
    void say (const std::string& line, bool toStderr = false) { journal (line, toStderr); }
    void stopHelpers();                     // the window and the announcer

    ServiceOptions options;
    Journal journal;
    Truth truth;
    Clock& clock;

    UdpSocket selfSocket, announceSocket;
    std::optional<UdpSocket> mainSocket, vuSocket;
    std::string lastReport;                    // the serve loop's last word, on the serve thread
    int mainBuffer = 0, selfBuffer = 0;
    std::unique_ptr<UdpNet> net;
    std::unique_ptr<Core> core;
    std::unique_ptr<HttpServer> window;
    std::string windowHost;

    std::thread serveThread, announceThread;
    std::atomic<bool> serving { false };
    std::mutex announceLock;
    std::condition_variable announceWake;
    bool announcing = false;
    std::mutex finishLock;
    std::condition_variable finishWake;
    std::optional<std::string> finishedBecause;
};
}
