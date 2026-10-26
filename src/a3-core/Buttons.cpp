// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Buttons.h"

namespace a3core
{
namespace
{
// Where a state divides. MixerState::channelToggle is `value > 0.5f`, strictly.
constexpr double onAbove = 0.5;

// A state, not an edge. A bool passes: it is an int in Python, and an OSC boolean is as much a
// state as a float. Text, blobs, None and the tuples python-osc decodes do not.
bool isANumber (const OscArg& value)
{
    switch (value.tag)
    {
        case 'i': case 'h': case 'r': case 'f': case 'd': case 'T': case 'F': return true;
        default: return false;
    }
}

bool isText (const OscArg& value, const std::string& text)
{
    const auto* held = std::get_if<std::string> (&value.value);
    return value.tag == 's' && held != nullptr && *held == text;
}
}

std::variant<NoChange, bool> wantedToggle (const OscArg& value, bool current)
{
    if (isANumber (value))
        return *pythonFloat (value) > onAbove;

    if (isText (value, "1"))
        return ! current;

    return NoChange {};
}

std::string wantedFxMode (const OscArg& value, const std::string& current)
{
    if (isANumber (value))
        return *pythonFloat (value) > onAbove ? fxModeHighPass : fxModeLowPass;

    if (isText (value, fxModeHighPass))
        return fxModeHighPass;
    if (isText (value, fxModeLowPass))
        return fxModeLowPass;

    return current;
}
}
