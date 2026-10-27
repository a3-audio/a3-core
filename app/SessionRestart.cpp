// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SessionRestart.h"

namespace a3app
{
namespace
{
// Opens up to `attempts` times; a failed one is dropped before the next. The last reason is returned.
juce::Result openWithRetries (const RestartSteps& steps, const std::function<juce::Result()>& open)
{
    auto result = juce::Result::fail ("not tried");
    for (int attempt = 0; attempt < steps.attempts; ++attempt)
    {
        if (attempt > 0)
            steps.pauseMs (steps.pauseBetweenAttempts);
        result = open();
        if (result.wasOk())
            return result;
        steps.dropNew();
    }
    return result;
}
}

RestartResult runRestart (const RestartSteps& steps)
{
    steps.quietOld();
    steps.dropOld();
    const auto opened = openWithRetries (steps, steps.openNew);
    if (opened.wasOk())
        return { RestartOutcome::applied, {} };

    const auto reopened = openWithRetries (steps, steps.openPrevious);
    if (reopened.wasOk())
        return { RestartOutcome::previousRunning, opened.getErrorMessage() };
    return { RestartOutcome::nothingRunning, opened.getErrorMessage() + "; the previous setting did not open either: " + reopened.getErrorMessage() };
}
}
