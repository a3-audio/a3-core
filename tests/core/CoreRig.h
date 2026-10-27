// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// One a3core::Core on a fake network, a fake clock and a temporary state directory, as
// tests/core/scenarios run it: what it sends itself is fed back from the socket the hello gate
// trusts. Shared by the router's suites (CoreValues, CorePeers, CoreMaps).
#pragma once

#include <algorithm>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "B0Truth.h"
#include "Core.h"
#include "FakeNet.h"
#include "Json.h"
#include "OscWire.h"

namespace corerig
{
using namespace a3core;

inline const std::string otherHash = std::string (60, '0') + "beef";

inline const std::string& ownHash()
{
    static const auto hash = b0::truth().fingerprint();
    return hash;
}

using Files = std::map<std::string, std::string>;   // a state file's name -> its text

class CoreRig
{
public:
    static constexpr int selfPort = 50000;

    const Endpoint desk { "192.168.8.11", 50001 };
    const Endpoint motion { "127.0.0.1", 40001 };
    const Endpoint remoteMotion { "192.168.8.20", 40002 };
    const Endpoint engine { "127.0.0.1", 41000 };
    const Endpoint analyzer { "127.0.0.1", 45000 };
    const Endpoint deckA { "127.0.0.2", 6000 };
    const Endpoint stranger { "192.168.8.50", 9999 };

    struct Options
    {
        Files files = {};
        std::optional<Endpoint> mixer = {};
        const Truth* truth = nullptr;   // b0::truth() when none
        bool start = true;              // false: Core is built, start() is the test's
    };

    struct WithOptions {};   // tells the two constructors apart: a braced file list is no Options

    explicit CoreRig (const Files& files = {}, std::optional<Endpoint> mixer = {}) : CoreRig (WithOptions {}, Options { files, mixer }) {}

    CoreRig (WithOptions, const Options& options)
        : truth (options.truth != nullptr ? *options.truth : b0::truth()), net (clock),
          stateDir (juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a3-core-rig", "", false))
    {
        const auto coreDir = stateDir.getChildFile ("a3-core");
        coreDir.createDirectory();
        for (const auto& [name, text] : options.files)
            coreDir.getChildFile (name).replaceWithText (text, false, false, "\n");
        net.toSelf = [this] (const std::vector<uint8_t>& data) { toSelf.push_back (data); };
        CoreArgs coreArgs { truth.host ("any"), truth.port ("core", "osc"), options.mixer.value_or (truth.endpoint ("mixer", "osc")),
                            truth.endpoint ("motion", "osc"), {} };
        CoreEnvironment environment { truth, net, clock,
                                      [this] (const std::string& line, bool toStderr) { net.journal (line, toStderr); },
                                      stateDir,
                                      [] { return std::string ("core-boot-0001"); },
                                      [this] { return 1000000 + ++tokens; },
                                      { loopbackAddress(), selfPort } };
        core = std::make_unique<Core> (environment, coreArgs);
        if (options.start)
            start();
    }

    ~CoreRig()
    {
        core.reset();
        stateDir.deleteRecursively();
    }

    void start()
    {
        core->start();
        feedBack();
    }

    void at (double seconds) { clock.at = seconds; }

    void osc (const Endpoint& from, const std::string& address, std::vector<OscArg> args = {})
    {
        const auto data = encodeMessage ({ address, std::move (args) });
        core->handlePacket (from, data.data(), data.size());
        feedBack();
    }

    void hello (const Endpoint& from, const std::string& device, const std::string& hash = ownHash())
    {
        osc (from, "/device/hello", { pyStr (device), pyStr (hash) });
    }

    void motionHello (const std::string& hash = ownHash()) { hello (motion, "motion", hash); }
    void engineHello (const std::string& bootId) { osc (engine, "/engine/hello", { pyStr (bootId) }); }
    void stemdeckHello (const std::string& host) { hello ({ host, 7000 }, "stemdeck"); }
    void deckHello (int deck, const Endpoint& from) { osc (from, "/stemdeck/" + std::to_string (deck) + "/hello", { pyStr ("x") }); }
    void beat (std::vector<OscArg> args) { osc (analyzer, "/beat", std::move (args)); }

    void vu (const std::vector<uint8_t>& data)
    {
        core->handleVu ({ loopbackAddress(), 47000 }, data.data(), data.size());
        feedBack();
    }

    void tick()
    {
        core->tick();
        feedBack();
    }

    void stop() { core->stop(); }

    void clear() { net.transcript.clear(); }

    // What one client was sent, as "address tag:value ...".
    std::vector<std::string> to (const std::string& client) const
    {
        std::vector<std::string> lines;
        for (const auto& entry : net.transcript)
            if (entry["to"].toString() == juce::String (client) && entry.hasProperty ("address"))
                lines.push_back (lineOf (entry));
        return lines;
    }

    // Where one client's datagrams went, as "host:port", once per datagram.
    std::vector<std::string> targetsOf (const std::string& client) const
    {
        std::vector<std::string> targets;
        for (const auto& entry : net.transcript)
            if (entry["to"].toString() == juce::String (client))
                targets.push_back (entry["host"].toString().toStdString() + ":" + entry["port"].toString().toStdString());
        return targets;
    }

    // Every send, as "client address tag:value ...".
    std::vector<std::string> sendLines() const
    {
        std::vector<std::string> lines;
        for (const auto& entry : net.transcript)
            if (entry.hasProperty ("address"))
                lines.push_back (entry["to"].toString().toStdString() + " " + lineOf (entry));
        return lines;
    }

    std::vector<std::string> journal() const
    {
        std::vector<std::string> lines;
        for (const auto& entry : net.transcript)
            if (entry.hasProperty ("journal"))
                lines.push_back (entry["journal"].toString().toStdString());
        return lines;
    }

    size_t sends() const
    {
        return static_cast<size_t> (std::count_if (net.transcript.begin(), net.transcript.end(),
                                                   [] (const juce::var& entry) { return entry.hasProperty ("address"); }));
    }

    long long unknownCount (const std::string& address)
    {
        for (const auto& row : core->traffic().unknownSnapshot (clock.monotonic()).rows)
            if (row.address == address)
                return row.count;
        return 0;
    }

    size_t unknownRows() { return core->traffic().unknownSnapshot (clock.monotonic()).rows.size(); }

    bool seenIn (const std::string& address)
    {
        for (const auto& row : core->traffic().snapshot (std::nullopt, clock.monotonic()).rows)
            if (row.direction == Direction::in && row.address == address)
                return true;
        return false;
    }

    // The peer the traffic table names for the last arrival on `address`.
    std::string peerOf (const std::string& address)
    {
        for (const auto& row : core->traffic().snapshot (std::nullopt, clock.monotonic()).rows)
            if (row.direction == Direction::in && row.address == address)
                return row.peer;
        return {};
    }

    juce::File file (const std::string& name) const { return stateDir.getChildFile ("a3-core").getChildFile (name); }

    juce::var json (const std::string& name) const
    {
        juce::String error;
        return parseJson (file (name).loadFileAsString(), error);
    }

    const Truth& truth;
    fakenet::FakeClock clock;
    fakenet::FakeNet net;
    std::unique_ptr<Core> core;

private:
    static std::string lineOf (const juce::var& entry)
    {
        std::ostringstream line;
        line << entry["address"].toString().toStdString();
        for (const auto& arg : *entry["args"].getArray())
        {
            const auto tag = arg[0].toString().toStdString();
            line << " " << tag;
            if (tag == "f")
                line << ":" << std::setprecision (6) << static_cast<float> (static_cast<double> (arg[1]));
            else if (tag == "i" || tag == "h")
                line << ":" << static_cast<juce::int64> (arg[1]);
            else if (tag == "s")
                line << ":" << arg[1].toString().toStdString();
        }
        return line.str();
    }

    void feedBack()
    {
        while (! toSelf.empty())
        {
            std::vector<std::vector<uint8_t>> waiting;
            waiting.swap (toSelf);
            for (const auto& data : waiting)
                core->handlePacket ({ loopbackAddress(), selfPort }, data.data(), data.size());
        }
    }

    juce::File stateDir;
    long long tokens = 0;
    std::vector<std::vector<uint8_t>> toSelf;
};

inline bool has (const std::vector<std::string>& lines, const std::string& line)
{
    return std::find (lines.begin(), lines.end(), line) != lines.end();
}

inline long count (const std::vector<std::string>& lines, const std::string& line) { return std::count (lines.begin(), lines.end(), line); }

inline std::vector<std::string> startingWith (const std::vector<std::string>& lines, const std::string& prefix)
{
    std::vector<std::string> out;
    for (const auto& line : lines)
        if (line.rfind (prefix, 0) == 0)
            out.push_back (line);
    return out;
}
}
