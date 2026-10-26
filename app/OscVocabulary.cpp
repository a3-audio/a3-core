// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OscVocabulary.h"
#include <algorithm>
#include <cmath>

namespace a3app
{
namespace
{
struct WordKey { EngineWord word; const char* key; };

// engine.osc's carries, in EngineWord order (R5: Core's words, read by name).
const WordKey wordKeys[] {
    { EngineWord::channelGain, "channel.gain" },
    { EngineWord::channelEqHigh, "channel.eq.high" },
    { EngineWord::channelEqMid, "channel.eq.mid" },
    { EngineWord::channelEqLow, "channel.eq.low" },
    { EngineWord::channelVolume, "channel.volume" },
    { EngineWord::channelThreeD, "channel.3d" },
    { EngineWord::channelFilterFrequency, "channel.filter.frequency" },
    { EngineWord::channelFilterQ, "channel.filter.q" },
    { EngineWord::channelAuxSend, "channel.aux-send" },
    { EngineWord::channelCue, "channel.cue" },
    { EngineWord::channelFilter, "channel.filter" },
    { EngineWord::channelAzimuth, "channel.azimuth" },
    { EngineWord::channelElevation, "channel.elevation" },
    { EngineWord::channelSource, "channel.source" },
    { EngineWord::filterFrequency, "filter.frequency" },
    { EngineWord::filterResonance, "filter.resonance" },
    { EngineWord::filterMode, "filter.mode" },
    { EngineWord::masterVolume, "master.volume" },
    { EngineWord::masterBooth, "master.booth" },
    { EngineWord::masterPhonesMix, "master.phones-mix" },
    { EngineWord::masterPhonesVolume, "master.phones-volume" },
    { EngineWord::masterAuxReturn, "master.aux-return" },
    { EngineWord::returnSource, "aux-return.source" },
    { EngineWord::returnCue, "aux-return.cue" },
    { EngineWord::delayBpm, "delay.bpm" },
    { EngineWord::engineOpen, "engine.open" },
};

constexpr float minBpm = 20.0f, maxBpm = 400.0f;      // Core's tempo follower's range (delay.bpm)
constexpr float maxAzimuth = 180.0f, maxElevation = 90.0f;
constexpr float onAbove = 0.5f;                        // Core's _ON_ABOVE: a number above it is "on"

// The one argument as a number; nullopt for a string, any other type or a non-finite value.
std::optional<double> numberOf (const juce::OSCMessage& m)
{
    const auto& arg = m[0];
    double value = 0.0;
    if (arg.isFloat32())
        value = arg.getFloat32();
    else if (arg.isInt32())
        value = arg.getInt32();
    else
        return std::nullopt;
    if (! std::isfinite (value))
        return std::nullopt;
    return value;
}

// A whole number in first..last; nullopt for a fraction or a number outside.
std::optional<int> wholeIn (const juce::OSCMessage& m, int first, int last)
{
    const auto value = numberOf (m);
    if (! value || *value != std::floor (*value) || *value < first || *value > last)
        return std::nullopt;
    return (int) *value;
}

std::optional<int> filterModeOf (const juce::OSCMessage& m)
{
    const auto& arg = m[0];
    if (arg.isString())
    {
        if (arg.getString() == "high_pass")
            return 1;
        if (arg.getString() == "low_pass")
            return 0;
        return std::nullopt;
    }
    const auto value = numberOf (m);
    if (! value)
        return std::nullopt;
    return *value > onAbove ? 1 : 0;
}

bool storeClamped (std::atomic<float>& target, const juce::OSCMessage& m, float low, float high)
{
    const auto value = numberOf (m);
    if (! value)
        return false;
    target.store (std::clamp ((float) *value, low, high));
    return true;
}

bool storeUnit (std::atomic<float>& target, const juce::OSCMessage& m) { return storeClamped (target, m, 0.0f, 1.0f); }

bool storeWhole (std::atomic<int>& target, const juce::OSCMessage& m, int first, int last)
{
    const auto value = wholeIn (m, first, last);
    if (value)
        target.store (*value);
    return value.has_value();
}

// channel: the channel's controls for a "{ch}" word, nullptr for a global one.
bool applyWord (EngineWord word, const a3::EngineConfig& config, const juce::OSCMessage& m,
                a3::ChannelControls* ch, int channelIndex, a3::GlobalControls& g)
{
    switch (word)
    {
        case EngineWord::channelGain: return ch != nullptr && storeUnit (ch->gain, m);
        case EngineWord::channelEqHigh: return ch != nullptr && storeUnit (ch->eqHigh, m);
        case EngineWord::channelEqMid: return ch != nullptr && storeUnit (ch->eqMid, m);
        case EngineWord::channelEqLow: return ch != nullptr && storeUnit (ch->eqLow, m);
        case EngineWord::channelVolume: return ch != nullptr && storeUnit (ch->volume, m);
        case EngineWord::channelThreeD: return ch != nullptr && storeUnit (ch->threeD, m);
        case EngineWord::channelFilterFrequency: return ch != nullptr && storeUnit (ch->frequency, m);
        case EngineWord::channelFilterQ: return ch != nullptr && storeUnit (ch->q, m);
        case EngineWord::channelAuxSend: return ch != nullptr && storeUnit (ch->auxSend, m);
        case EngineWord::channelCue: return ch != nullptr && storeUnit (ch->cue, m);
        case EngineWord::channelFilter: return ch != nullptr && storeUnit (ch->filterOn, m);
        case EngineWord::channelAzimuth: return ch != nullptr && storeClamped (ch->azimuth, m, -maxAzimuth, maxAzimuth);
        case EngineWord::channelElevation: return ch != nullptr && storeClamped (ch->elevation, m, -maxElevation, maxElevation);
        case EngineWord::channelSource:
        {
            if (ch == nullptr)
                return false;
            const bool hasAnalog = (size_t) channelIndex < config.analogPairs.size()
                                   && config.analogPairs[(size_t) channelIndex] > 0;
            return storeWhole (ch->source, m, hasAnalog ? 0 : 1, config.decks);   // R10: a deck-only channel has no analog
        }
        case EngineWord::filterFrequency: return storeUnit (g.filter.frequency, m);
        case EngineWord::filterResonance: return storeUnit (g.filter.resonance, m);
        case EngineWord::filterMode:
        {
            const auto mode = filterModeOf (m);
            if (mode)
                g.filter.mode.store (*mode);
            return mode.has_value();
        }
        case EngineWord::masterVolume: return storeUnit (g.masterVolume, m);
        case EngineWord::masterBooth: return storeUnit (g.booth, m);
        case EngineWord::masterPhonesMix: return storeUnit (g.phonesMix, m);
        case EngineWord::masterPhonesVolume: return storeUnit (g.phonesVolume, m);
        case EngineWord::masterAuxReturn: return storeUnit (g.auxReturn, m);
        case EngineWord::returnSource: return storeWhole (g.returnSource, m, 0, config.decks);
        case EngineWord::returnCue: return storeUnit (g.returnCue, m);
        case EngineWord::delayBpm: return storeClamped (g.delayBpm, m, minBpm, maxBpm);
        case EngineWord::engineOpen:
        {
            const auto open = wholeIn (m, 0, 1);   // 1 opens, 0 closes; nothing else opens (spec Review Focus 2)
            if (open)
                g.gateOpen.store (*open == 1);
            return open.has_value();
        }
    }
    return false;
}
}

const std::vector<juce::String>& engineWordKeys()
{
    static const std::vector<juce::String> keys = []
    {
        std::vector<juce::String> list;
        for (const auto& w : wordKeys)
            list.emplace_back (w.key);
        return list;
    }();
    return keys;
}

std::optional<EngineTruth> parseEngineTruth (const juce::String& json, juce::String& error)
{
    const auto base = a3bench::parseTruth (json);
    if (! base)
    {
        error = "not the OSC truth, or it lacks what the bench part needs (engine.osc, the channel keys, the vu pattern)";
        return std::nullopt;
    }
    EngineTruth truth;
    static_cast<a3bench::Truth&> (truth) = *base;

    const auto root = juce::JSON::parse (json);
    const auto addresses = root["addresses"];
    for (const auto& w : wordKeys)
    {
        const auto entry = addresses[juce::Identifier (w.key)];
        const auto pattern = entry["pattern"].toString();
        if (pattern.isEmpty())
        {
            error = juce::String ("the truth has no address \"") + w.key + "\"";
            return std::nullopt;
        }
        WordAddress word { w.word, w.key, {}, {} };
        if (pattern.contains ("{ch}"))
        {
            word.channel = a3bench::channelPatternOf (entry);
            if (! word.channel)
            {
                error = juce::String ("\"") + w.key + "\" has no channel range";
                return std::nullopt;
            }
        }
        else
        {
            word.address = pattern;
        }
        truth.words.push_back (word);
    }

    truth.helloAddress = addresses["engine.hello"]["pattern"].toString();
    if (truth.helloAddress.isEmpty())
        return error = "the truth has no address \"engine.hello\"", std::nullopt;
    truth.corePort = a3bench::listenerPort (root, "core.osc");
    truth.coreHost = a3bench::listenerHostAddress (root, "core.osc");
    if (truth.coreHost == root["hosts"]["any"].toString())   // a wildcard listener is reached on loopback (a3_osc endpoint)
        truth.coreHost = root["hosts"]["local"].toString();
    if (truth.corePort <= 0 || truth.coreHost.isEmpty())
        return error = "the truth has no listener \"core.osc\" with a host and a port", std::nullopt;
    return truth;
}

bool applyEngineMessage (const EngineTruth& truth, const a3::EngineConfig& config, const juce::OSCMessage& m,
                         a3::EngineControls& controls)
{
    if (m.size() != 1)
        return false;
    const auto address = m.getAddressPattern().toString();
    const int channels = std::min (controls.channels(), config.channels);
    for (const auto& word : truth.words)
    {
        if (word.channel)
        {
            const int channel = a3bench::channelOfAddress (*word.channel, address, channels);
            if (channel != 0)
                return applyWord (word.word, config, m, &controls.channel (channel - 1), channel - 1, controls.global());
            continue;
        }
        if (address == word.address)
            return applyWord (word.word, config, m, nullptr, -1, controls.global());
    }
    return false;
}
}
