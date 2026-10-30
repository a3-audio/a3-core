// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// `a3-core --role engine`: Plan 4's engine app without its window. Loads the truth, engine.json and
// the spatial data, builds the host on the chosen backend (JACK on the rig, a JUCE device elsewhere),
// listens for Core on engine.osc, says hello to core.osc and drives the host from the app's timer.
// The window's pages reach a running engine over the control API (P2, EngineControlApi.h), served here on
// engine.control; the window is another process.
#pragma once

#include <memory>
#include <optional>
#include <variant>

#include "AppArgs.h"
#include "Roles.h"

namespace a3core
{
class HttpServer;
}

namespace a3app
{
class EngineSession;
class EngineView;
class EngineControlApi;

class EngineRole final : public RoleRunner
{
public:
    // The running engine, or the exit code when the role ends at once: --check-config's answer, or 1
    // for a start that is refused (said on stderr).
    static std::variant<std::unique_ptr<EngineRole>, int> start (const AppArgs&, const juce::File& home);

    ~EngineRole() override;
    std::optional<int> tick (bool stopRequested) override;
    int tickHz() const override { return 100; }   // the /vu sends (25 Hz) are due on a 10 ms grid

private:
    friend class EngineView;
    EngineRole (const AppArgs&, StartupData);

    // Save of a changed config: the whole session is dropped and built again (new host, new boot id, new
    // ports; Core replays through the hello). The old gates close first (no hard cut on the PA); the old
    // session closes before the new one opens because both bind engine.osc and may hold the device. If the
    // new config does not open the old one is opened again; if even that fails the role stays up without an
    // engine and says so in `problem` (the STATUS page shows it).
    juce::Result restart (const AppConfig&, juce::String& problem);
    void serveControl();
    void storeConverterShare();

    AppArgs args;
    StartupData startup;
    // Declared in the order they are built: the server stops first, then the API, then the view, then the session.
    std::unique_ptr<EngineSession> session;
    std::unique_ptr<EngineView> view;
    std::unique_ptr<EngineControlApi> api;
    std::unique_ptr<a3core::HttpServer> server;
};
}
