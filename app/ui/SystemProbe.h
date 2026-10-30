// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What RIG and ABOUT show, read off the message thread (the Canvas's StatusPoller): systemd's word on the rig's
// units and Core's /api/rig and /api/status once a second while the page is shown; every 30 s the umbrella checkout's
// pins and which units are the Core's (the a3-core and a3-core-app packages' unit files, a3-main's Wants=); this
// machine's addresses. A plan (Start all, Stop Deck A, ...) runs on a thread of its own. Commands go
// through a CommandRunner (posix_spawn, never fork: the window must not copy its page tables on a timer). The page
// reads snapshot(), which never waits for a command or a socket.
#pragma once
#include "RigPlan.h"
#include "SystemAbout.h"
#include "SystemCheckout.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace a3core { class Truth; }

namespace a3app
{
struct SystemSources
{
    std::shared_ptr<CommandRunner> runner;       // systemctl show, git; nullptr: nothing is asked
    std::shared_ptr<CommandRunner> planRunner;   // the plans' systemctl (a longer time limit); nullptr: no plan runs
    std::function<std::pair<juce::String, juce::String>()> askCore;   // Core's /api/rig and /api/status bodies, "" each when silent
    std::function<std::vector<rig::InterfaceAddress>()> interfaces;
    rig::Network truth;                          // the truth's hosts and networks (own: filled by each poll)
    juce::File checkout;                         // the umbrella checkout (~/a3-system)
    about::Support support;
    int decks = 0;
    std::function<bool()> deskInstalled;
};

struct SystemSnapshot
{
    std::uint64_t generation = 0;   // moves when anything new arrived
    bool polled = false;            // a poll has run
    rig::Facts facts;
    checkout::Facts checkout;
    bool busy = false;              // a plan runs
    juce::String planLine;          // the running plan's latest word, or the last one's outcome
};

class SystemProbe
{
public:
    static constexpr int pollPeriodMillis = 1000;
    static constexpr int checkoutPeriodMillis = 30000;

    // `startPolling` false: nothing runs until pollNow() (the tests drive it).
    explicit SystemProbe (SystemSources, bool startPolling = true);
    ~SystemProbe();
    SystemProbe (const SystemProbe&) = delete;
    SystemProbe& operator= (const SystemProbe&) = delete;

    // The page is shown: only then is the machine asked anything.
    void setWanted (bool shown);
    bool isWanted() const { return wanted; }
    // One poll on the caller's thread, the checkout included.
    void pollNow();
    SystemSnapshot snapshot() const;
    const std::vector<rig::Program>& programs() const { return rigPrograms; }
    const about::Support& support() const { return sources.support; }

    // Runs `steps` on a thread of its own, its log line by line into planLine; false while one runs (or no runner).
    bool run (const juce::String& title, std::vector<rig::Step> steps);
    // Waits for the running plan to end (the tests).
    void waitForPlan();

private:
    void loop();
    void poll (bool withCheckout);
    void say (const juce::String& line, bool ended);

    const SystemSources sources;
    const std::vector<rig::Program> rigPrograms;
    std::atomic<bool> wanted { false }, stopping { false }, busy { false };
    mutable std::mutex lock;
    std::mutex pollLock;   // one poll at a time: the thread's or pollNow's
    SystemSnapshot latest;
    std::vector<rig::ServiceUnit> services;   // the Core's units here, read with the checkout
    std::uint32_t checkoutReadAt = 0;
    bool checkoutRead = false;
    std::mutex wakeLock;
    std::condition_variable wake;
    std::thread poller, planner;
};

// This machine: systemctl and git through posix_spawn, Core at the truth's core.web, the truth's hosts, ~/a3-system,
// the Canvas's support file under `configDir`.
SystemSources machineSystemSources (const a3core::Truth&, const juce::File& home, const juce::File& configDir, int decks);
}
