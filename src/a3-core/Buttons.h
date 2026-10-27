// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_buttons.py (B0 f3345dc7feeb): what a button message asks Core to do, when two
// senders mean two things. The A3 Mixer relays its serial line as text ("1" down, "0" up: an
// edge); A3 Motion sends the state it wants as a number. Core tells them apart by the argument's
// type -- a coupling with a3-mixer.py, which must keep sending text.
#pragma once

#include <string>
#include <variant>

#include "OscWire.h"

namespace a3core
{
// What a release asks for: nothing. A type of its own, with no conversion to bool, so that
// "asks for nothing" cannot be merged with "asks for off" (Python's NO_CHANGE raises when
// truth-tested for the same reason).
struct NoChange
{
};

// The two words /fx/mode carries, and the only two the LED reply knows.
inline const std::string fxModeHighPass = "high_pass";
inline const std::string fxModeLowPass = "low_pass";

// The state a button message asks a toggle to be in, or NoChange.
std::variant<NoChange, bool> wantedToggle (const OscArg& value, bool current);

// The filter mode a /fx/mode message asks for; `current` for anything it cannot read.
std::string wantedFxMode (const OscArg& value, const std::string& current);
}
