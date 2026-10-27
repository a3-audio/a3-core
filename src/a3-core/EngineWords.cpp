// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineWords.h"

namespace a3core
{
namespace
{
const std::set<std::string> sentAsTheyArrived {
    "channel.gain", "channel.eq.high", "channel.eq.mid", "channel.eq.low",
    "channel.volume", "channel.3d", "channel.filter.frequency",
    "channel.filter.q", "channel.aux-send", "filter.frequency",
    "filter.resonance", "master.volume", "master.booth", "master.phones-mix",
    "master.phones-volume", "master.aux-return",
};

// A deck channel's own 3D and filter go to the deck or nowhere, never to the engine's analog path.
const std::set<std::string> notForDeckChannels { "channel.3d", "channel.filter.frequency", "channel.filter.q" };

int wire (int channelIndex) { return channelIndex + 1; }

const char* flagKey (EngineFlag flag) { return flag == EngineFlag::cue ? "channel.cue" : "channel.filter"; }

const char* positionKey (Position word) { return word == Position::azimuth ? "channel.azimuth" : "channel.elevation"; }

void addFlags (const Truth& truth, const std::vector<ChannelInfo>& channels, std::vector<Message>& out)
{
    for (size_t index = 0; index < channels.size(); ++index)
    {
        out.push_back (flagMessage (truth, EngineFlag::cue, static_cast<int> (index), channels[index].toggleCue));
        out.push_back (flagMessage (truth, EngineFlag::filter, static_cast<int> (index), channels[index].toggleFx));
    }
}

void addPositions (const Truth& truth, const std::vector<ChannelInfo>& channels, const DeckSelector& selector, std::vector<Message>& out)
{
    for (size_t index = 0; index < channels.size(); ++index)
    {
        if (selector.deckChannel (wire (static_cast<int> (index))))
            continue;
        if (channels[index].azimuth)
            out.push_back (positionMessage (truth, Position::azimuth, static_cast<int> (index), *channels[index].azimuth));
        if (channels[index].elevation)
            out.push_back (positionMessage (truth, Position::elevation, static_cast<int> (index), *channels[index].elevation));
    }
}
}

int engineReturnSource (int returnMode) { return returnMode == Stems::stemMode ? returnDelay : 0; }

const std::set<std::string>& forwarded() { return sentAsTheyArrived; }

std::vector<Message> forwardMessages (const Truth& truth, const std::string& key, const std::map<std::string, int>& fields, double value)
{
    if (! sentAsTheyArrived.count (key))
        return {};
    return { { truth.address (key, fields), pyFloat (value) } };
}

std::vector<Message> engineChannelMessages (const Truth& truth, const DeckSelector& selector, const std::string& key,
                                            const std::map<std::string, int>& fields, double value)
{
    const auto channel = fields.find ("ch");
    if (notForDeckChannels.count (key) && channel != fields.end() && selector.deckChannel (channel->second))
        return {};
    return forwardMessages (truth, key, fields, value);
}

std::vector<Message> analogAgainMessages (const Truth& truth, int channelIndex, const ChannelInfo& info, const Relayed& relayed)
{
    const auto number = wire (channelIndex);
    std::vector<Message> messages;
    if (info.threeD)
        messages.push_back ({ truth.address ("channel.3d", { { "ch", number } }), pyFloat (*info.threeD) });
    for (const char* key : { "channel.filter.frequency", "channel.filter.q" })
    {
        const auto address = truth.address (key, { { "ch", number } });
        if (const auto value = relayed.value (address))
            if (const auto asNumber = pythonFloat (*value))
                messages.push_back ({ address, pyFloat (*asNumber) });
    }
    if (info.azimuth)
        messages.push_back (positionMessage (truth, Position::azimuth, channelIndex, *info.azimuth));
    if (info.elevation)
        messages.push_back (positionMessage (truth, Position::elevation, channelIndex, *info.elevation));
    return messages;
}

Message flagMessage (const Truth& truth, EngineFlag flag, int channelIndex, bool on)
{
    return { truth.address (flagKey (flag), { { "ch", wire (channelIndex) } }), pyFloat (on ? 1.0 : 0.0) };
}

Message filterModeMessage (const Truth& truth, int modeNumber) { return { truth.address ("filter.mode"), pyFloat (modeNumber) }; }

Message positionMessage (const Truth& truth, Position word, int channelIndex, double degrees)
{
    return { truth.address (positionKey (word), { { "ch", wire (channelIndex) } }), pyFloat (degrees) };
}

Message returnCueMessage (const Truth& truth, bool on) { return { truth.address ("aux-return.cue"), pyFloat (on ? 1.0 : 0.0) }; }

Message tempoMessage (const Truth& truth, double bpm) { return { truth.address ("delay.bpm"), pyFloat (bpm) }; }

Message openMessage (const Truth& truth) { return { truth.address ("engine.open"), pyInt (1) }; }

std::vector<Message> sourceMessages (const Truth& truth, const DeckSelector& selector, int returnSource)
{
    std::vector<Message> messages;
    for (int channel = 1; channel <= selector.channels(); ++channel)
        messages.push_back ({ truth.address ("channel.source", { { "ch", channel } }), pyInt (selector.source (channel)) });
    messages.push_back ({ truth.address ("aux-return.source"), pyInt (returnSource) });
    return messages;
}

std::vector<Message> stateMessages (const Truth& truth, const std::vector<ChannelInfo>& channels, const MasterInfo& master,
                                    const DeckSelector& selector, int returnSource, std::optional<double> bpm)
{
    std::vector<Message> messages;
    addFlags (truth, channels, messages);
    messages.push_back (filterModeMessage (truth, static_cast<int> (master.fxMode)));
    messages.push_back (returnCueMessage (truth, master.returnCue));
    for (auto& message : sourceMessages (truth, selector, returnSource))
        messages.push_back (std::move (message));
    addPositions (truth, channels, selector, messages);
    if (bpm)
        messages.push_back (tempoMessage (truth, *bpm));
    return messages;
}

EngineSession::EngineSession (Evening evening, std::function<Evening()> currentEvening)
    : fromFile (std::move (evening)), current (std::move (currentEvening))
{
}

bool EngineSession::heardHello (const OscArg& id)
{
    if (bootId && sameAsPython (*bootId, id))
        return false;
    bootId = id;
    ++round;
    return true;
}

std::optional<std::string> EngineSession::lastBootId() const
{
    if (! bootId)
        return std::nullopt;
    const auto* text = std::get_if<std::string> (&bootId->value);
    return text != nullptr ? std::optional<std::string> (*text) : std::nullopt;
}

Evening EngineSession::evening() const { return live ? current() : fromFile; }

bool EngineSession::replayed (int roundStarted)
{
    if (roundStarted != round)
        return false;
    live = true;
    return true;
}
}
