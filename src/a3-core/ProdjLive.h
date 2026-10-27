// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_prodj_live.py: whether a pro-DJ app may move the room, held as a lease. Off at
// start; while on, repeated every repeatEvery seconds. Pure; serve thread only, so no lock.
#pragma once

#include <optional>
#include <string>

namespace a3core
{
inline constexpr double repeatEvery = 5.0;

class ProdjLive
{
public:
    bool on() const { return isOn; }
    // True when the state changed.
    bool set (bool wanted, double now);
    // Switch off because the last prodj went silent; true when it changed.
    bool nobodyLeft();
    // While on: true once every repeatEvery seconds.
    bool due (double now);
    // Whether a word from `kind` (a kind, a full name such as "prodj@h", or nullopt for an origin
    // without one) may act. Only a prodj is held back, and only while off; the pointers are a
    // prodj's alone, and only while on.
    bool admits (const std::optional<std::string>& kind, const std::string& key) const;

private:
    bool isOn = false;
    std::optional<double> lastSent;
};

// Whether `origin` may switch LIVE to `wanted`. On only while a pro-DJ app is followed; off is
// always taken, from whoever may switch; Motion only ever to off.
bool maySwitch (const std::string& originKindOrName, bool wanted, bool prodjFollowed);
}
