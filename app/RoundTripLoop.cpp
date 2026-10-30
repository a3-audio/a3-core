// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "RoundTripLoop.h"

namespace a3app
{
namespace
{
// The whole number after `prefix` at the end of `name`, or nullopt.
std::optional<int> numberAfter (const std::string& name, const std::string& prefix)
{
    const auto at = name.rfind (prefix);
    if (at == std::string::npos)
        return std::nullopt;
    const auto digits = name.substr (at + prefix.size());
    if (digits.empty() || digits.size() > 4 || digits.find_first_not_of ("0123456789") != std::string::npos)
        return std::nullopt;
    return std::stoi (digits);
}

bool startsWith (const std::string& text, const std::string& prefix) { return text.rfind (prefix, 0) == 0; }
bool holds (const std::string& text, const std::string& part) { return text.find (part) != std::string::npos; }
}

std::optional<PeerChannel> deviceChannelOfPeer (const std::string& port)
{
    for (const bool capture : { true, false })
    {
        const std::string kind = capture ? "capture_" : "playback_";
        if (startsWith (port, "system:" + kind))
            if (const auto n = numberAfter (port, ":" + kind); n && *n >= 1)
                return PeerChannel { capture, *n };
        if (holds (port, capture ? ".pro-input-" : ".pro-output-"))
            if (const auto n = numberAfter (port, ":" + kind + "AUX"))
                return PeerChannel { capture, *n + 1 };
    }
    return std::nullopt;
}

bool loopHolds (const std::optional<RoundTripLoop>& loop, bool input, int channel)
{
    if (! loop)
        return false;
    const int first = input ? loop->input : loop->output;
    return channel >= first && channel < first + loop->channels;
}

bool isLoopPeer (const std::optional<RoundTripLoop>& loop, const std::string& port)
{
    const auto channel = deviceChannelOfPeer (port);
    return channel && loopHolds (loop, channel->capture, channel->channel);
}

juce::String loopRangeText (const RoundTripLoop& loop, bool input)
{
    const int first = input ? loop.input : loop.output;
    const juce::String side = input ? "input " : "output ";
    return side + juce::String (first) + (loop.channels > 1 ? "-" + juce::String (first + loop.channels - 1) : juce::String());
}
}
