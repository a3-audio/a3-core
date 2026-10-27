// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_engine.py (B1 7a7b2441f74e): Core's words to the a3-engine, and the session that
// says when to replay them. In engine mode Core sends the engine the wire value as it arrived; the
// engine applies the laws (zero is silence there, ruling R18). Nothing here adds one.
//
// Every message carries the OSC type of the Python value: a flag, a position, a tempo, a mode and
// a forwarded value are floats ('f'); the sources and engine.open are integers ('i') -- the engine
// refuses a fraction where a whole number is due.
//
// Pure: no sockets. Channel arguments are 0-based indexes as in Core's channel list; the addresses
// carry the 1-based wire number.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Channels.h"
#include "Decks.h"
#include "OscWire.h"
#include "Recall.h"
#include "Stems.h"
#include "Truth.h"

namespace a3core
{
// What the engine is replayed at its start: the values Core last passed on.
using Evening = std::vector<Message>;

// The deck whose AUX pair the return plays in STEM mode.
inline constexpr int returnDeck = 1;

// The aux return's source from the desk's own return word: STEM plays deck A's AUX pair, ANALOG analog 11/12.
int engineReturnSource (int returnMode);

// The truth names sent as they arrived.
const std::set<std::string>& forwarded();

// `value` as a float, if `key` is one of the forwarded words.
std::vector<Message> forwardMessages (const Truth& truth, const std::string& key, const std::map<std::string, int>& fields, double value);

// The same, except that a deck channel's own 3D and filter go to the deck or nowhere, never to the engine's analog path.
std::vector<Message> engineChannelMessages (const Truth& truth, const DeckSelector& selector, const std::string& key,
                                            const std::map<std::string, int>& fields, double value);

enum class EngineFlag { cue, filter, fx };   // Core's name for the filter toggle is "fx"; the wire says filter
enum class Position { azimuth, elevation };

// What the engine's analog path missed while a deck held the channel: its 3D, FREQ, Q and position,
// from Core's own state. Unknown values are left unsaid.
std::vector<Message> analogAgainMessages (const Truth& truth, int channelIndex, const ChannelInfo& info, const Relayed& relayed);

Message flagMessage (const Truth& truth, EngineFlag flag, int channelIndex, bool on);
Message filterModeMessage (const Truth& truth, int modeNumber);
Message positionMessage (const Truth& truth, Position word, int channelIndex, double degrees);
Message returnCueMessage (const Truth& truth, bool on);
Message tempoMessage (const Truth& truth, double bpm);
Message openMessage (const Truth& truth);   // engine.open as the integer 1
// Every channel's source from the deck selector, then the return's: integers.
std::vector<Message> sourceMessages (const Truth& truth, const DeckSelector& selector, int returnSource);

// What the engine needs that the evening's replay does not carry.
std::vector<Message> stateMessages (const Truth& truth, const std::vector<ChannelInfo>& channels, const MasterInfo& master,
                                    const DeckSelector& selector, int returnSource, std::optional<double> bpm);

// Whether what the engine reports is the evening. A boot id not heard before is an engine that has
// just started and knows nothing: it gets the replay. The same id again is its repeated hello.
class EngineSession
{
public:
    EngineSession (Evening evening, std::function<Evening()> current);

    // True when the engine gets the replay.
    bool heardHello (const OscArg& bootId);
    // The file's evening until the first replay completed, then Core's own.
    Evening evening() const;
    // False when a newer hello overtook the replay that began in `roundStarted`.
    bool replayed (int roundStarted);

    bool live = false;
    int round = 0;

private:
    Evening fromFile;
    std::function<Evening()> current;
    std::optional<OscArg> bootId;
};
}
