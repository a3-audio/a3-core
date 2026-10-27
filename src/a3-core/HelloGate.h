// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_hello_gate.py: whose mixer values count -- only those of a host that has said
// hello with Core's own truth. Pure: the caller passes the address and does the logging.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <utility>

#include "Net.h"

namespace a3core
{
enum class GateVerdict { admit, refuse, refuseFirst, refuseDiffersFirst };

class HelloGate
{
public:
    // A hello from `host`; `matches` is whether its truth fingerprint is Core's own. Only a match
    // opens the host; a differing one closes it.
    void greet (const std::string& host, bool matches);
    // Admit this exact (host, port) -- Core's own replay socket.
    void trust (const Endpoint& source);
    // Admit every value from `host`, hello or not -- the desk at its configured address.
    void trustHost (const std::string& host);
    // admit, refuse, or the first refusal of a host (the one worth a line in the journal).
    GateVerdict check (const std::string& host, std::optional<int> port = std::nullopt);

private:
    std::set<std::string> greeted, told, trustedHosts, differs, toldDiffers;
    std::set<std::pair<std::string, int>> trusted;
};
}
