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

namespace
{
void addPeerOnce (a3::links::LinkMap& map, const std::string& port, const std::string& peer)
{
    auto& peers = map.jack[port];
    if (std::find (peers.begin(), peers.end(), peer) == peers.end())
        peers.push_back (peer);
}
}

void addRecToAes67Sender (a3::links::LinkMap& map, const std::string& hostname)
{
    if (hostname.empty())
        return;
    const auto sender = "AES67 " + hostname + " out:";
    for (const auto& [port, channel] : { std::pair<const char*, const char*> { "rec_L", "send_CH1" }, { "rec_R", "send_CH2" } })
        addPeerOnce (map, port, sender + channel);
}

void addStreamSenders (a3::links::LinkMap& map, const std::vector<a3core::StreamSpec>& streams)
{
    for (const auto& spec : streams)
    {
        if (spec.legacy)
            continue;
        const auto channels = std::min (spec.enginePorts.size(), spec.channelNames.size());
        for (std::size_t k = 0; k < channels; ++k)
            addPeerOnce (map, spec.enginePorts[k], spec.session + ":send_" + spec.channelNames[k]);
    }
}

void addBeatAnalyzerLink (a3::links::LinkMap& map) { addPeerOnce (map, "rec_L", beatAnalyzerInput); }

bool hasAnalogTestPair (const IoMap& map) { return map.analogLoop && map.analogLoop->output > 0 && map.analogLoop->input > 0; }

void addRoundTripLoopLinks (IoMap& map)
{
    if (map.roundTripLoop)
    {
        map.jack[loopOutputPort] = { "system:playback_" + std::to_string (map.roundTripLoop->output) };
        map.jack[loopInputPort] = { "system:capture_" + std::to_string (map.roundTripLoop->input) };
    }
    if (hasAnalogTestPair (map))
    {
        map.jack[analogOutputPort] = { "system:playback_" + std::to_string (map.analogLoop->output) };
        map.jack[analogInputPort] = { "system:capture_" + std::to_string (map.analogLoop->input) };
    }
}

IoMap linkMapFor (const AppConfig& config, const std::string& hostname, const std::vector<a3core::StreamSpec>& streams)
{
    auto map = config.io;
    addBeatAnalyzerLink (map);
    addRecToAes67Sender (map, hostname);
    addStreamSenders (map, streams);
    addRoundTripLoopLinks (map);
    return map;
}

bool linkPlanDue (bool connecting, bool portsChanged, double nowSeconds, double lastLinkSeconds)
{
    if (! connecting)
        return false;
    return portsChanged || nowSeconds - lastLinkSeconds >= linkEverySeconds;
}
}
