// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What the router's handlers share with B1's Python: an argument read as python-osc hands it on, and
// an exception raised where B1 raises. Core's own, used by CoreValues, CorePeers and CoreRecall.
#pragma once

#include <stdexcept>
#include <string>

#include "OscWire.h"

namespace a3core
{
// An exception a handler raises where B1's raises; what() is the Python type, which the serve loop
// reports (tests/core/scenarios/README.md, "A handler that raises").
struct PythonError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// osc_arguments[0]: an IndexError when there is none.
const OscArg& firstOf (const OscMessage& message);
// osc_arguments[0] if osc_arguments else None
const OscArg* firstOrNone (const OscMessage& message);
// float(x) of an argument as python-osc hands it on: a text or bytes that is no number is a
// ValueError, None, a MIDI tuple or a timetag a TypeError.
double floatOf (const OscArg& arg);
// str(x) of an argument as python-osc hands it on.
std::string pythonStr (const OscArg& arg);
// The first `count` characters, as Python slices a str.
std::string firstCharacters (const std::string& text, int count);
}
