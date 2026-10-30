// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Core's window as Core serves it -- the real monitor and core routes (CoreApi.h) on the real HTTP server,
// on a port the system chooses -- over a traffic table, devices and a status the test fills in, with the
// client in front: what the window's monitor pages see in the program. The bench's sends are recorded;
// a send to "mixer" is refused as Core's sender refuses one it cannot make.
#pragma once
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

#include "CoreApi.h"
#include "CoreClient.h"
#include "HttpServer.h"
#include "Json.h"
#include "Subscribers.h"
#include "Traffic.h"
#include "Truth.h"

namespace a3apptest
{
struct SteppedClock : a3core::Clock
{
    std::atomic<double> mono { 100.0 };
    double monotonic() override { return mono; }
    double wall() override { return 0; }
};

inline a3core::Truth truthCopy()
{
    auto loaded = a3core::Truth::load (juce::File (A3_TRUTH_COPY), std::nullopt);
    if (auto* problem = std::get_if<a3core::TruthError> (&loaded))
        throw std::runtime_error ("the truth copy: " + problem->what);
    return std::get<a3core::Truth> (std::move (loaded));
}

struct CoreWindowRig
{
    a3core::Truth truth = truthCopy();
    a3core::Traffic traffic;
    a3core::Devices devices { truth.fingerprint() };
    SteppedClock clock;
    a3core::CoreView view;
    juce::File registerFile = juce::File::createTempFile ("osc-register.json");
    std::vector<std::pair<std::string, a3core::OscMessage>> sent;
    std::vector<std::vector<a3core::Department>> departmentsSaved;   // what POST /api/departments handed on
    std::vector<a3core::Subscriber> serviceSubscribers;               // what the report names as the service file's
    std::unique_ptr<a3core::HttpServer> server;
    std::unique_ptr<a3app::CoreClient> client;

    CoreWindowRig()
    {
        registerFile.replaceWithText (R"({"devices": ["motion", "mixer"], "directions": ["in", "out"], "entries": [
            {"address": "/channel/{ch}/volume", "device": "mixer", "direction": "in", "source": "a3-mixer", "note": ""},
            {"address": "/channel/{ch}/cue", "device": "mixer", "direction": "in", "source": "a3-mixer", "note": ""},
            {"address": "/channel/{ch}/azimuth", "device": "motion", "direction": "in", "source": "a3-motion", "note": "position"}]})");
        view.publish ({ R"({"audio_engine":"engine","reason":""})", R"({"motion":null})" });
        a3core::Routes routes;
        a3core::addMonitorRoutes (routes, { &truth, traffic, &devices, registerFile, clock, 20 });
        a3core::addCoreRoutes (routes, &view, [this] (const std::string& to, const a3core::OscMessage& message) {
            if (to == "mixer")
                throw a3core::SendRefused ("the mixer has no address yet");
            sent.emplace_back (to, message);
        });
        a3core::DepartmentsDoor door;
        door.resolves = [] (const std::string& host) { return host != "nowhere.invalid"; };
        door.save = [this] (const std::vector<a3core::Department>& list) {
            departmentsSaved.push_back (list);
            publishDepartments (list);
        };
        a3core::addDepartmentRoutes (routes, &view, std::move (door));
        server = std::make_unique<a3core::HttpServer> (a3core::loopbackAddress() + ":0", std::move (routes));
        if (! server->start ([] (const std::string&, bool) {}))
            throw std::runtime_error ("Core's window did not start");
        client = std::make_unique<a3app::CoreClient> (server->host(), server->port(), truth.pattern ("state.recall"), false);
    }

    ~CoreWindowRig()
    {
        client.reset();
        server->stop();
        registerFile.deleteFile();
    }

    // The report Core would publish for `stored` beside the service file's: what is on and not overridden is
    // subscribed and was sent `sent` messages, the last `ago` seconds before now.
    void publishDepartments (const std::vector<a3core::Department>& stored, long long sentCount = 3, double ago = 0.5)
    {
        juce::Array<juce::var> rows;
        const auto row = [&] (const std::string& name, const std::string& host, int port, bool on, const char* from, bool overridden) {
            auto* made = new juce::DynamicObject();
            const bool subscribed = on && ! overridden;
            made->setProperty ("name", juce::String (name));
            made->setProperty ("host", juce::String (host));
            made->setProperty ("port", port);
            made->setProperty ("on", on);
            made->setProperty ("from", juce::String (from));
            made->setProperty ("subscribed", subscribed);
            made->setProperty ("overridden", overridden);
            made->setProperty ("sent", subscribed ? juce::var (sentCount) : juce::var (0));
            made->setProperty ("last_sent", subscribed ? juce::var (ago) : juce::var());
            rows.add (juce::var (made));
        };
        for (const auto& subscriber : serviceSubscribers)
            row (subscriber.name, subscriber.host, subscriber.port, true, "service", false);
        for (const auto& entry : stored)
        {
            const bool overridden = std::any_of (serviceSubscribers.begin(), serviceSubscribers.end(),
                                                 [&] (const a3core::Subscriber& subscriber) { return subscriber.name == entry.name; });
            row (entry.name, entry.host, entry.port, entry.on, "window", overridden);
        }
        auto* report = new juce::DynamicObject();
        report->setProperty ("departments", rows);
        auto texts = view.texts();
        texts.departments = a3core::canonicalBytes (juce::var (report));
        view.publish (texts);
    }

    // A message Core understood, `seconds` before now.
    void seen (a3core::Direction direction, const std::string& address, double value, const std::string& peer, double ago = 0.0)
    {
        const auto arg = a3core::pyFloat (value);
        traffic.seen (direction, address, &arg, peer, clock.mono - ago);
    }

    // Some of everything: rows from two devices, one of them silent for long, an unknown address, two
    // devices' truths (one differs).
    void fill()
    {
        seen (a3core::Direction::in, "/channel/1/azimuth", 90.0, "motion", 30.0);   // first: stamps never go back
        seen (a3core::Direction::in, "/channel/1/volume", 0.5, "mixer", 1.0);
        seen (a3core::Direction::in, "/channel/1/volume", 0.6, "mixer");
        seen (a3core::Direction::out, "/vu/1", 0.25, "motion");
        const auto word = a3core::pyStr ("hello");
        traffic.unknown ("/nobody/knows", &word, "stranger", clock.mono);
        devices.heard ("motion", truth.fingerprint(), clock.mono);
        devices.heard ("stemdeck", "0123456789abcdef0123", clock.mono);
    }
};
}
