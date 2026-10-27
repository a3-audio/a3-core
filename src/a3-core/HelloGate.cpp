// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HelloGate.h"

namespace a3core
{
void HelloGate::greet (const std::string& host, bool matches)
{
    if (matches)
    {
        greeted.insert (host);
        differs.erase (host);
        return;
    }
    greeted.erase (host);
    differs.insert (host);
    toldDiffers.erase (host);
}

void HelloGate::trust (const Endpoint& source) { trusted.insert ({ source.host, source.port }); }

void HelloGate::trustHost (const std::string& host) { trustedHosts.insert (host); }

GateVerdict HelloGate::check (const std::string& host, std::optional<int> port)
{
    const bool replaySocket = port.has_value() && trusted.count ({ host, *port }) > 0;
    if (trustedHosts.count (host) > 0 || replaySocket || greeted.count (host) > 0)
        return GateVerdict::admit;
    if (differs.count (host) > 0)
    {
        if (! toldDiffers.insert (host).second)
            return GateVerdict::refuse;
        return GateVerdict::refuseDiffersFirst;
    }
    if (! told.insert (host).second)
        return GateVerdict::refuse;
    return GateVerdict::refuseFirst;
}
}
