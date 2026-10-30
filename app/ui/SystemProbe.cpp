// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemProbe.h"
#include "LoopbackHttp.h"
#include "Truth.h"

namespace a3app
{
namespace
{
constexpr int coreTimeoutMillis = 300;      // Core is on this machine or on the switch: it answers at once or not at all
constexpr int planTimeoutMillis = 30000;    // a systemctl start waits for its unit
constexpr int defaultDecks = 4;

juce::String activeStateOf (CommandRunner& runner, const juce::String& unit)
{
    return juce::String (runner.run ({ "systemctl", "--user", "is-active", unit.toStdString() }).output).trim();
}
}

SystemProbe::SystemProbe (SystemSources s, bool startPolling)
    : sources (std::move (s)), rigPrograms (rig::programs (sources.decks > 0 ? sources.decks : defaultDecks))
{
    if (startPolling)
        poller = std::thread ([this] { loop(); });
}

SystemProbe::~SystemProbe()
{
    stopping = true;
    wake.notify_all();
    if (poller.joinable())
        poller.join();
    if (planner.joinable())
        planner.join();   // a running systemctl ends within its own time limit
}

void SystemProbe::setWanted (bool shown)
{
    if (wanted.exchange (shown) == shown)
        return;
    wake.notify_all();
}

void SystemProbe::pollNow() { poll (true); }

SystemSnapshot SystemProbe::snapshot() const
{
    const std::lock_guard<std::mutex> guard (lock);
    return latest;
}

void SystemProbe::loop()
{
    while (! stopping)
    {
        if (wanted)
        {
            const auto now = juce::Time::getMillisecondCounter();
            poll (! checkoutRead || now - checkoutReadAt >= (std::uint32_t) checkoutPeriodMillis);
        }
        std::unique_lock<std::mutex> sleep (wakeLock);
        wake.wait_for (sleep, std::chrono::milliseconds (pollPeriodMillis), [this] { return stopping.load(); });
    }
}

void SystemProbe::poll (bool withCheckout)
{
    const std::lock_guard<std::mutex> one (pollLock);
    rig::Facts facts;
    facts.network = sources.truth;
    if (sources.interfaces)
        facts.network.own = rig::ownAddresses (sources.interfaces(), sources.truth);
    if (sources.runner != nullptr)
        facts.units = rig::parseSystemctlShow (sources.runner->run (rig::showCommand (rigPrograms)).output);
    if (sources.askCore)
    {
        const auto [rigBody, statusBody] = sources.askCore();
        facts.core = rig::parseCore (rigBody, statusBody);
    }
    facts.deskInstalled = sources.deskInstalled && sources.deskInstalled();
    std::optional<checkout::Facts> pins;
    if (withCheckout && sources.runner != nullptr)
    {
        pins = checkout::read (*sources.runner, sources.checkout);
        checkoutReadAt = juce::Time::getMillisecondCounter();
        checkoutRead = true;
    }
    const std::lock_guard<std::mutex> guard (lock);
    latest.facts = std::move (facts);
    if (pins)
        latest.checkout = std::move (*pins);
    latest.polled = true;
    ++latest.generation;
}

void SystemProbe::say (const juce::String& line, bool ended)
{
    const std::lock_guard<std::mutex> guard (lock);
    latest.planLine = line;
    latest.busy = ! ended;
    ++latest.generation;
}

bool SystemProbe::run (const juce::String& title, std::vector<rig::Step> steps)
{
    if (sources.planRunner == nullptr || busy.exchange (true))
        return false;
    if (planner.joinable())
        planner.join();   // the last plan has ended: busy was false
    say (title + juce::String::fromUTF8 ("…"), false);
    planner = std::thread ([this, title, steps = std::move (steps)] {
        auto& runner = *sources.planRunner;
        const rig::Waiting waiting { 30, [this] { if (! stopping) juce::Thread::sleep (500); } };
        const auto result = rig::execute (steps, runner, [&runner] (const juce::String& unit) { return activeStateOf (runner, unit); },
                                          waiting, [this] (const juce::String& line) { say (line, false); });
        say (rig::outcome (title, result), true);
        busy = false;
        wake.notify_all();   // the page's state follows at the next poll; this one comes at once
    });
    return true;
}

void SystemProbe::waitForPlan()
{
    if (planner.joinable())
        planner.join();
}

SystemSources machineSystemSources (const a3core::Truth& truth, const juce::File& home, const juce::File& configDir, int decks)
{
    SystemSources sources;
    sources.runner = std::make_shared<SpawnRunner>();
    sources.planRunner = std::make_shared<SpawnRunner> (planTimeoutMillis);
    const auto web = truth.endpoint ("core", "web");
    sources.askCore = [web] {
        const auto get = [&web] (const char* path) {
            const auto reply = httpExchange (web.host, web.port, "GET", path, {}, coreTimeoutMillis);
            return reply && reply->status == 200 ? juce::String (reply->body) : juce::String();
        };
        return std::make_pair (get ("/api/rig"), get ("/api/status"));
    };
    sources.interfaces = rig::listInterfaces;
    sources.truth = rig::truthNetwork (truth.data());
    sources.checkout = home.getChildFile ("a3-system");
    sources.support = about::machineSupport (configDir);
    sources.decks = decks;
    sources.deskInstalled = [] { return juce::File ("/usr/bin/a3-desk").existsAsFile(); };
    return sources;
}
}
