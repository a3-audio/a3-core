// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of B1's a3-bar-per-workspace.py (a3-core 7a7b2441f74e): i3bar shows on the rig's tool workspaces
// and is hidden on the touch UIs. Workspaces 1 (A3 Motion) and 2 (StemDeck) fill their 768x1024 screen;
// from 3 on i3bar at the top is the way back; deck B on a workspace named STEMDECK fills its screen like
// deck A. i3 has no bar per workspace, so this follows i3's workspace events and sets the bar's mode.
#pragma once

#include <atomic>
#include <optional>
#include <string>

#include "Platform.h"

namespace a3core
{
inline constexpr const char* i3BarId = "a3";

// "dock" where the bar shows, "invisible" where the screen is a UI's.
std::string barMode (const std::string& workspaceName);

// The mode an i3 workspace event line asks for, or nullopt for a line that is not a focus change
// (another change, not JSON, not an object).
std::optional<std::string> focusChange (const std::string& eventLine);

// Sets the bar for the focused workspace, then follows the workspace events: i3-msg runs as child
// processes through the platform; the mode is set only when it changes. Returns the event stream's exit
// code when it ends, 0 when `stop` asked, 1 when i3-msg cannot be started.
int runI3Bar (const Platform&, const std::atomic<bool>* stop = nullptr);
}
