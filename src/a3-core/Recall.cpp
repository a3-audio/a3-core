// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Recall.h"

#include <cstring>

namespace a3core
{
namespace
{
bool lit (LedFlag flag, const ChannelInfo& channel) { return flag == LedFlag::cue ? channel.toggleCue : channel.toggleFx; }

// Each flag's address on the wire, next to its lamp's: a lamp is a light, a flag is a setting.
const char* flagKey (LedFlag flag) { return flag == LedFlag::cue ? "channel.cue" : "channel.filter"; }

Message flagMessage (const Truth& truth, LedFlag flag, int index, const ChannelInfo& channel)
{
    return { truth.address (flagKey (flag), { { "ch", index + 1 } }), pyFloat (lit (flag, channel) ? 1.0 : 0.0) };
}

// Numbers as Python compares them, whatever holds them; anything else by tag and value.
struct Number
{
    bool whole;
    long long integer;
    long double real;
};

std::optional<Number> numberOf (const OscArg& arg)
{
    switch (arg.tag)
    {
        case 'i': return Number { true, std::get<int32_t> (arg.value), 0 };
        case 'h': case 'r': case 't': return Number { true, std::get<int64_t> (arg.value), 0 };
        case 'T': return Number { true, 1, 0 };
        case 'F': return Number { true, 0, 0 };
        case 'f': case 'd':
            if (const auto* single = std::get_if<float> (&arg.value))
                return Number { false, 0, *single };
            return Number { false, 0, std::get<double> (arg.value) };
        default: return std::nullopt;
    }
}

bool sameAsPython (const OscArg& a, const OscArg& b)
{
    const auto left = numberOf (a);
    const auto right = numberOf (b);
    if (! left && ! right)
        return a.tag == b.tag && a.value == b.value;
    if (! left || ! right)
        return false;
    if (left->whole && right->whole)
        return left->integer == right->integer;
    const auto real = [] (const Number& n) { return n.whole ? static_cast<long double> (n.integer) : n.real; };
    return real (*left) == real (*right);
}
}

const char* ledKey (LedFlag flag) { return flag == LedFlag::cue ? "channel.cue.led" : "channel.filter.led"; }

Message ledMessage (const Truth& truth, LedFlag flag, int index, const ChannelInfo& channel)
{
    return { truth.address (ledKey (flag), { { "ch", index + 1 } }), pyFloat (lit (flag, channel) ? 1.0 : 0.0) };
}

Message returnCueLamp (const Truth& truth, const MasterInfo& master)
{
    return { truth.address ("aux-return.cue.led"), pyFloat (master.returnCue ? 1.0 : 0.0) };
}

std::vector<Message> rememberedMessages (const Truth& truth, const std::vector<ChannelInfo>& channels)
{
    std::vector<Message> out;
    for (size_t index = 0; index < channels.size(); ++index)
    {
        const std::map<std::string, int> at { { "ch", static_cast<int> (index) + 1 } };
        const auto& channel = channels[index];
        // `is None`, not falsiness: 0.0 is the front of the room, a real position.
        if (channel.azimuth) out.emplace_back (truth.address ("channel.azimuth", at), pyFloat (*channel.azimuth));
        if (channel.elevation) out.emplace_back (truth.address ("channel.elevation", at), pyFloat (*channel.elevation));
        if (channel.threeD) out.emplace_back (truth.address ("channel.3d", at), pyFloat (*channel.threeD));
    }
    return out;
}

std::vector<Message> lampMessages (const Truth& truth, const std::vector<ChannelInfo>& channels, const MasterInfo& master)
{
    std::vector<Message> out;
    for (size_t index = 0; index < channels.size(); ++index)
        for (const auto flag : ledFlags)
            out.push_back (ledMessage (truth, flag, static_cast<int> (index), channels[index]));
    out.emplace_back (truth.address ("filter.led"), pyStr (fxModeWords.at (master.fxMode)));
    out.push_back (returnCueLamp (truth, master));
    return out;
}

std::vector<Message> flagMessages (const Truth& truth, const std::vector<ChannelInfo>& channels, const MasterInfo& master)
{
    std::vector<Message> out;
    for (size_t index = 0; index < channels.size(); ++index)
        for (const auto flag : ledFlags)
            out.push_back (flagMessage (truth, flag, static_cast<int> (index), channels[index]));
    out.emplace_back (truth.address ("filter.mode"), pyFloat (fxModeNumbers.at (master.fxMode)));
    return out;
}

bool Relayed::holds (const std::string& address, const OscArg& value) const
{
    const auto found = place.find (address);
    return found != place.end() && sameAsPython (held[found->second].second, value);
}

void Relayed::note (const std::string& address, OscArg value)
{
    const auto found = place.find (address);
    if (found != place.end())
    {
        held[found->second].second = std::move (value);
        return;
    }
    place.emplace (address, held.size());
    held.emplace_back (address, std::move (value));
}

std::vector<Message> recallMessages (const Truth& truth, const std::vector<ChannelInfo>& channels, const MasterInfo& master,
                                     const Relayed& relayed)
{
    auto out = lampMessages (truth, channels, master);
    const auto append = [&out] (const std::vector<Message>& more) { out.insert (out.end(), more.begin(), more.end()); };
    append (flagMessages (truth, channels, master));
    append (rememberedMessages (truth, channels));
    append (relayed.messages());
    return out;
}
}
