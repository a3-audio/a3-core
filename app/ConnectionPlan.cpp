// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ConnectionPlan.h"
#include <algorithm>

namespace a3app
{
juce::Result refuseBesideReaper (const std::vector<std::string>& clients, bool connecting)
{
    const bool reaper = std::find (clients.begin(), clients.end(), "REAPER") != clients.end();
    if (connecting && reaper)
        return juce::Result::fail ("REAPER is running: start with --no-connect");
    return juce::Result::ok();
}

void addRecToAes67Sender (a3::links::LinkMap& map, const std::string& hostname)
{
    if (hostname.empty())
        return;
    const auto sender = "AES67 " + hostname + " out:";
    for (const auto& [port, channel] : { std::pair<const char*, const char*> { "rec_L", "send_CH1" }, { "rec_R", "send_CH2" } })
    {
        auto& peers = map.jack[port];
        const auto peer = sender + channel;
        if (std::find (peers.begin(), peers.end(), peer) == peers.end())
            peers.push_back (peer);
    }
}

IoMap linkMapFor (const AppConfig& config, const std::string& hostname)
{
    auto map = config.io;
    addRecToAes67Sender (map, hostname);
    return map;
}

bool linkPlanDue (bool connecting, bool portsChanged, double nowSeconds, double lastLinkSeconds)
{
    if (! connecting)
        return false;
    return portsChanged || nowSeconds - lastLinkSeconds >= linkEverySeconds;
}
}
