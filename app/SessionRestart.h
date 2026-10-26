// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <juce_core/juce_core.h>

namespace a3app
{
// The order of a Save that re-creates the session, with the app's pieces as callbacks so the
// order and the failure branches are testable without a JACK server or a device.
struct RestartSteps
{
    std::function<void()> quietOld;                 // close the old gate and wait for the fade: the PA hears no cut
    std::function<void()> dropOld;                  // close the old session (backend, socket, host)
    std::function<juce::Result()> openNew;          // build and open the session for the new config
    std::function<void()> dropNew;                  // close a session that did not open
    std::function<juce::Result()> openPrevious;     // build and open the session for the previous config
    std::function<void (int)> pauseMs;
    int attempts = 3;                               // per open: a name clash with the client just closed is transient
    int pauseBetweenAttempts = 100;
};

enum class RestartOutcome { applied, previousRunning, nothingRunning };
struct RestartResult
{
    RestartOutcome outcome = RestartOutcome::applied;
    juce::String message;   // why the new config did not open; for nothingRunning, both reasons
};

RestartResult runRestart (const RestartSteps&);
}
