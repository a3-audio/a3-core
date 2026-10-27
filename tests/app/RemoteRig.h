// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A fake engine behind the real control API and the real HTTP server, on a port the system chooses,
// with the remote view in front: what the window's pages see in the program. The test thread is the
// engine's control thread: tick() runs what the routes queued, and settle() lets a call of the view's
// finish while that happens.
#pragma once
#include <chrono>
#include <future>
#include <memory>
#include "../core/HttpClient.h"
#include "AppTruth.h"
#include "EngineControlApi.h"
#include "FakeView.h"
#include "RemoteHostView.h"

namespace a3apptest
{
inline a3app::EngineTruth truthOnAnyFreePort()
{
    auto truth = engineTruth();
    truth.controlPort = 0;
    return truth;
}

struct RemoteRig
{
    FakeView fake;
    a3app::EngineControlApi api { fake };
    std::vector<std::string> said;
    std::unique_ptr<a3core::HttpServer> server;
    std::unique_ptr<a3app::RemoteHostView> remote;
    double now = 1.0;

    RemoteRig()
    {
        server = a3app::serveEngineControl (api, truthOnAnyFreePort(), [this] (const std::string& line, bool) { said.push_back (line); });
        if (server == nullptr)
            throw std::runtime_error ("the control API did not start");
        api.tick (now);
        remote = std::make_unique<a3app::RemoteHostView> (server->host(), server->port(), false);
    }
    ~RemoteRig() { api.close(); }

    // Each tick is past both publish periods: what the test changed in the fake is served after it.
    void tick() { api.tick (now += 1.0); }

    // Runs `call` on another thread while this one is the control thread.
    template <class F>
    auto settle (F call)
    {
        auto done = std::async (std::launch::async, std::move (call));
        while (done.wait_for (std::chrono::milliseconds (2)) != std::future_status::ready)
            tick();
        return done.get();
    }

    // The posts of the remote view reach the engine, run on its control thread, and the cache follows.
    void sync()
    {
        settle ([this] { remote->refreshNow(); return 0; });
        tick();
        settle ([this] { remote->refreshNow(); return 0; });
    }
};
}
