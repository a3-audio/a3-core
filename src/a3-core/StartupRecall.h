// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_gate.StartupRecall (B1 7a7b2441f74e; plan ruling P1 -- live in engine mode): the
// one /state/recall Core sends itself after the evening replay.
//
// Core's command loop is serial, so that recall is handled after every replayed value -- the point
// at which the replay has been applied. A device's recall carries 1 and may arrive at any time, so
// Core's own carries a random token instead (still an OSC int, as the truth says), and only the
// first one carrying it counts.
#pragma once

#include <optional>
#include <vector>

#include "OscWire.h"

namespace a3core
{
// 2 + secrets.randbelow(2 ** 30): never 0 or 1, which a device sends, and an OSC int.
long long randomToken();

class StartupRecall
{
public:
    explicit StartupRecall (std::optional<long long> token = std::nullopt) { rearm (token); }

    // A new token for the next replay, after the engine came back under a running Core: the recall
    // that follows it opens the gate again. Returns the token.
    long long rearm (std::optional<long long> token = std::nullopt);
    // Whether these are the arguments of Core's own recall, and the first time they are.
    bool isIt (const std::vector<OscArg>& arguments);

    long long token() const { return armed; }

private:
    long long armed = 0;
    bool taken = false;
};
}
