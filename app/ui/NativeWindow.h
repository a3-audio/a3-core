// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Whether a window is on the screen as X sees it. JUCE's isShowing() does not know: i3 shows another workspace by
// unmapping this one's frame, which leaves the window IsUnviewable without a word to JUCE (and without the
// _NET_WM_STATE_HIDDEN JUCE reads as minimised; i3 4.25 on the rig, 2026-10-31).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace a3app::native
{
// False only when `component`'s window is on X and not viewable; true without a window or off X.
bool viewable (const juce::Component& component);
}
