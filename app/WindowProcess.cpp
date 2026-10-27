// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "WindowProcess.h"

#include <algorithm>

#include "Roles.h"

namespace a3app
{
WindowProcess::WindowProcess (const a3core::Platform& platformToUse, CoreClient& coreToUse, RemoteHostView& engineToUse)
    : platform (platformToUse), core (coreToUse), engine (engineToUse) {}

WindowProcess::~WindowProcess() = default;   // a child left here runs on: owning it does not end it

bool WindowProcess::coreAnswers() const { return core.answers(); }
bool WindowProcess::engineAnswers() const { return engine.answers(); }

std::vector<Role> WindowProcess::attach()
{
    const auto running = children();
    std::vector<Role> missing;
    const auto wanted = [&] (Role role, bool answers) {
        if (! answers && std::find (running.begin(), running.end(), role) == running.end())
            missing.push_back (role);
    };
    wanted (Role::core, coreAnswers());
    wanted (Role::engine, engineAnswers());

    std::vector<Role> startedNow;
    for (const auto role : startableRoles (platform, missing))
    {
        // Its own process group: a child the window may leave running must not die with the terminal
        // the window was started from (Ctrl-C, or the terminal closing).
        auto child = platform.spawn (roleCommand (platform.self().getFullPathName(), role), false, a3core::ProcessGroup::own);
        if (child == nullptr)
            continue;
        started.push_back ({ role, std::move (child) });
        startedNow.push_back (role);
    }
    return startedNow;
}

std::vector<Role> WindowProcess::children()
{
    std::vector<Role> running;
    for (auto& own : started)
        if (! own.child->poll())
            running.push_back (own.role);
    return running;
}

std::optional<juce::String> WindowProcess::quitQuestion()
{
    if (asked)
        return std::nullopt;
    const auto running = children();
    if (running.empty())
        return std::nullopt;
    asked = true;
    return stopQuestion (running);
}

void WindowProcess::quit (bool stopChildren)
{
    if (! stopChildren)
        return;
    for (auto& own : started)
        if (! own.child->poll())
            own.child->terminate();
    for (auto& own : started)
        if (! own.child->wait (stopWaitSeconds))
            own.child->kill();
}

juce::String stopQuestion (const std::vector<Role>& children)
{
    const bool core = std::find (children.begin(), children.end(), Role::core) != children.end();
    const bool engine = std::find (children.begin(), children.end(), Role::engine) != children.end();
    if (core && engine)
        return "Stop Core and the engine too?";
    return core ? "Stop Core too?" : "Stop the engine too?";
}

// Shared with the answer's callback: once the flow is gone, `process` is null and the answer does nothing.
struct QuitFlow::State
{
    WindowProcess* process;
    bool asking = false, finished = false;
};

QuitFlow::QuitFlow (WindowProcess& process, Ask askToUse) : state (std::make_shared<State> (State { &process })), ask (std::move (askToUse)) {}

QuitFlow::~QuitFlow() { state->process = nullptr; }

void QuitFlow::close()
{
    if (state->asking || state->finished)
        return;   // the question is open: only its answer ends the window
    const auto question = state->process->quitQuestion();
    if (! question)
    {
        state->finished = true;
        return;
    }
    state->asking = true;
    ask (*question, [weak = std::weak_ptr<State> (state)] (bool stop) {
        const auto alive = weak.lock();
        if (alive == nullptr || alive->process == nullptr)
            return;
        alive->process->quit (stop);
        alive->asking = false;
        alive->finished = true;
    });
}

void QuitFlow::signal()
{
    if (! state->asking)
        state->finished = true;   // a signal asks nobody; what the window started runs on
}

bool QuitFlow::done() const { return state->finished; }
}
