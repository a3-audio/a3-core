// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The two things the keyboard needs from the machine: asking onboard over D-Bus and starting it.
// Ported from StemDeck (OnScreenKeyboard.cpp), where they were free functions; here they sit behind
// a seam so the keyboard's failure paths are tested with a fake, and the real one goes through
// a3core::Platform like the rest of the app.
#pragma once
#include <memory>
#include <string>

#include "Platform.h"

namespace a3app
{
class OnboardLink
{
public:
    virtual ~OnboardLink() = default;
    // onboard and dbus-send are both on the PATH.
    virtual bool installed() const = 0;
    // org.onboard.Onboard.Keyboard.<method> ("Show", "Hide", "ToggleVisible"); true when onboard took the call.
    virtual bool call (const std::string& method) = 0;
    // Starts onboard in its own process group (a Ctrl-C in a terminal leaves it). It stays this program's child in the
    // service's cgroup, so under systemd it dies with the unit. false: it could not be started.
    virtual bool launch() = 0;
};

// The real one, over the platform's environment and child processes. `replyTimeoutMs` is dbus-send's own
// limit; the link waits a little longer for the process.
std::shared_ptr<OnboardLink> makeOnboardLink (const a3core::Platform&, int replyTimeoutMs = 1000);
}
