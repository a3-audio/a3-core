// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_recall.py (B0 f3345dc7feeb; Relayed::value from B1 7a7b2441f74e): Core says its state once more. There is no new
// message format for that: Core replays every value exactly as it would have been sent when it
// changed, so the receivers that already exist take it. The answer is the lamps and the flags
// (Core's own, read off disk), then the positions and the crossfade (Core's own, because nobody
// else can be asked), then what Core last passed on. A cold Core answers short, on purpose: an
// answer a caller can act on beats silence.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Channels.h"
#include "OscWire.h"
#include "Truth.h"

namespace a3core
{
using Message = std::pair<std::string, OscArg>;

// a == b as Python compares two values an OSC message carried: numbers by value (1 == 1.0 == True), anything else by tag and value.
bool sameAsPython (const OscArg& a, const OscArg& b);

// The two flags a channel carries, and the lamp each one lights.
enum class LedFlag { cue, fx };
inline constexpr std::array<LedFlag, 2> ledFlags { LedFlag::cue, LedFlag::fx };

// The truth's key of the channel lamp for a flag.
const char* ledKey (LedFlag flag);

// How each filter mode is spelled on the wire. Written out rather than derived from the enum: a
// rename of a member would otherwise be a silent change to the protocol.
inline const std::map<MasterInfo::FxMode, std::string> fxModeWords { { FxMode::lowPass, "low_pass" }, { FxMode::highPass, "high_pass" } };
// How the filter mode reads as a number on `/filter/mode`: 1 is high pass.
inline const std::map<MasterInfo::FxMode, double> fxModeNumbers { { FxMode::highPass, 1.0 }, { FxMode::lowPass, 0.0 } };

// The one message that says what a channel's `flag` lamp should do (index counts from 0).
Message ledMessage (const Truth& truth, LedFlag flag, int index, const ChannelInfo& channel);

// The aux return's cue lamp: the toggle and the recall say it through this one function.
Message returnCueLamp (const Truth& truth, const MasterInfo& master);

// What Core holds for each channel because nobody else can be asked, as the messages that set it:
// azimuth, elevation, 3d. A value never seen is left out rather than sent as 0.0.
std::vector<Message> rememberedMessages (const Truth& truth, const std::vector<ChannelInfo>& channels);

// What every lamp shows: two a channel, the filter's word, the return cue.
std::vector<Message> lampMessages (const Truth& truth, const std::vector<ChannelInfo>& channels, const MasterInfo& master);

// The same flags as settings, on the addresses they arrive on: 0 or 1, and the filter mode as a number.
std::vector<Message> flagMessages (const Truth& truth, const std::vector<ChannelInfo>& channels, const MasterInfo& master);

// The last value Core passed on, per address, in the order the controls were first touched.
// In memory only: it cannot be older than the last message the engine sent. A value is
// one argument, or a Python list -- the return's word is nine integers -- which python-osc sends as
// several arguments.
class Relayed
{
public:
    // Whether this is already what was last passed on (Python's ==: 1 == 1.0 == True; a list is
    // never one number, and equals a list of the same length whose items are equal).
    bool holds (const std::string& address, const OscArg& value) const;
    bool holds (const std::string& address, const std::vector<OscArg>& list) const;
    void note (const std::string& address, OscArg value);
    void note (const std::string& address, std::vector<OscArg> list);
    // The last value passed on for this address; none before any, and none for a list (its one
    // reader, analog_again_messages, reads the filter's words, which never hold one).
    std::optional<OscArg> value (const std::string& address) const;
    bool isList (const std::string& address) const;
    // Each value as the message that says it again: a list as several arguments.
    std::vector<OscMessage> messages() const;
    size_t size() const { return held.size(); }

private:
    struct Held
    {
        std::vector<OscArg> args;
        bool list = false;
    };

    void put (const std::string& address, Held value);
    const Held* find (const std::string& address) const;

    std::vector<std::pair<std::string, Held>> held;
    std::unordered_map<std::string, size_t> place;
};

// The whole answer: lamps, flags, positions and crossfade, then what was relayed.
std::vector<OscMessage> recallMessages (const Truth& truth, const std::vector<ChannelInfo>& channels, const MasterInfo& master,
                                     const Relayed& relayed);
}
