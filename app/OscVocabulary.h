// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <vector>
#include <juce_osc/juce_osc.h>
#include "EngineConfig.h"
#include "EngineControls.h"
#include "OscControl.h"

namespace a3app
{
// What a word of Core's sets in the engine, and how its value is read.
enum class EngineWord
{
    channelGain, channelEqHigh, channelEqMid, channelEqLow, channelVolume, channelThreeD,
    channelFilterFrequency, channelFilterQ, channelAuxSend, channelCue, channelFilter,
    channelAzimuth, channelElevation, channelSource,
    filterFrequency, filterResonance, filterMode,
    masterVolume, masterBooth, masterPhonesMix, masterPhonesVolume, masterAuxReturn,
    returnSource, returnCue, delayBpm, engineOpen
};

// One key of the truth the engine carries, with its address read from the truth.
struct WordAddress
{
    EngineWord word;
    juce::String key;                                // "channel.gain"
    juce::String address;                            // the fixed address, "" for a channel word
    std::optional<a3bench::ChannelPattern> channel;  // set for a "{ch}" word
};

// Everything the engine takes from a3-osc.json: the bench's part, the words of
// engine.osc's carries by name, where Core listens and the hello's address.
struct EngineTruth : a3bench::Truth
{
    int corePort = -1;
    juce::String coreHost;        // "core.osc"'s host; a wildcard listener is reached on loopback, as Core's endpoint()
    juce::String helloAddress;    // "engine.hello"
    juce::String deckAudioPattern;   // "engine.deck-audio", "{deck}" still in it; empty in a truth without the key
    int vuRelayPort = -1;         // "core.vu-relay": where the engine's /vu bundles go (O13); -1: none in this truth
    juce::String vuRelayHost;     // its host, a wildcard reached on loopback
    int controlPort = -1;         // "engine.control": where the window reaches the engine (loopback, HTTP); -1: none in this truth
    juce::String controlHost;     // its host as the truth gives it
    std::vector<WordAddress> words;
};

// "" when the truth has everything; else a line saying what is off (no deck-audio reports).
juce::String truthNote (const EngineTruth&);

// The truth keys of the words above, in EngineWord order.
const std::vector<juce::String>& engineWordKeys();

// nullopt with a reason in `error` for text that is not the truth or lacks a key or
// listener the engine needs.
std::optional<EngineTruth> parseEngineTruth (const juce::String& json, juce::String& error);

// Core's message into the controls, read and clamped as Core and the bench do:
// positions +-180 / +-90 degrees, 0..1 values, the source 0..decks (analog refused on a
// deck-only channel), the return source 0 analog / 1 delay, delay.bpm 20..400, filter.mode a
// number (> 0.5 high-pass) or "high_pass"/"low_pass", engine.open 1 opens and 0 closes.
// The channel is the truth's range within 1..config.channels. false, and nothing
// changed: an address the engine does not carry, not exactly one argument, a
// non-finite number, a string where a number is due, a fraction where a whole number is.
bool applyEngineMessage (const EngineTruth&, const a3::EngineConfig&, const juce::OSCMessage&, a3::EngineControls&);
}
