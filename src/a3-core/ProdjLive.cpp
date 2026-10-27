// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProdjLive.h"

#include <set>

#include "Desks.h"

namespace a3core
{
namespace
{
// A prodj that is off may still say hello, ask for the recall and switch on.
bool alwaysAdmittedFromAProdj (const std::string& key)
{
    static const std::set<std::string> keys { "device.hello", "state.recall", "prodj.live" };
    return keys.count (key) > 0;
}

// Who may send -- not the same decision as what Core relays or replays.
bool isPointer (const std::string& key) { return key == "stemdeck.pointer" || key == "motion.pointer"; }

std::string kindOfNameOrKind (const std::string& kindOrName)
{
    if (kindOrName.empty())
        return kindOrName;
    const auto kind = kindOf (kindOrName);
    return kind.has_value() && ! kind->empty() ? *kind : kindOrName;
}
}

bool ProdjLive::set (bool wanted, double now)
{
    if (wanted == isOn)
        return false;
    isOn = wanted;
    lastSent = wanted ? std::optional<double> (now) : std::nullopt;
    return true;
}

bool ProdjLive::nobodyLeft()
{
    if (! isOn)
        return false;
    isOn = false;
    lastSent.reset();
    return true;
}

bool ProdjLive::due (double now)
{
    if (! isOn || now - *lastSent < repeatEvery)
        return false;
    lastSent = now;
    return true;
}

bool ProdjLive::admits (const std::optional<std::string>& kindOrName, const std::string& key) const
{
    const bool prodj = kindOrName.has_value() && kindOfNameOrKind (*kindOrName) == prodjKind;
    if (isPointer (key))
        return prodj && isOn;
    if (! prodj)
        return true;
    return isOn || alwaysAdmittedFromAProdj (key);
}

bool maySwitch (const std::string& originKindOrName, bool wanted, bool prodjFollowed)
{
    const auto kind = kindOfNameOrKind (originKindOrName);
    if (wanted && ! prodjFollowed)
        return false;
    if (kind == "motion")
        return ! wanted;
    return kind == deskKind || kind == prodjKind || kind == "motion";
}
}
