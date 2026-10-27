// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_engine.py (B1 7a7b2441f74e): Core's words to the
// a3-engine and the session that decides when to replay them. Pure: no sockets. Channels in the
// arguments are 0-based indexes as in Core's channel list; the addresses carry the 1-based wire
// number. Every message is compared with the OSC type python-osc would give the Python value.
#include <set>

#include <gtest/gtest.h>

#include "B0Truth.h"
#include "EngineWords.h"
#include "Evening.h"

using namespace a3core;
using Fields = std::map<std::string, int>;

namespace
{
const Truth& truth() { return b0::truth(); }

DeckSelector selectorWith (std::vector<int> sources)
{
    juce::Array<juce::var> listed;
    for (int source : sources)
        listed.add (source);
    auto* object = new juce::DynamicObject();
    object->setProperty ("channel.source", juce::var (listed));
    DeckSelector selector;
    selector.fromEvening (juce::var (object));
    return selector;
}

std::vector<ChannelInfo> channels (int n = 4) { return std::vector<ChannelInfo> (static_cast<size_t> (n)); }

MasterInfo master (FxMode mode = FxMode::lowPass, bool returnCue = false)
{
    MasterInfo info;
    info.fxMode = mode;
    info.returnCue = returnCue;
    return info;
}

bool contains (const std::vector<Message>& messages, const Message& wanted)
{
    return std::find (messages.begin(), messages.end(), wanted) != messages.end();
}

bool startsWith (const std::string& text, const std::string& prefix) { return text.rfind (prefix, 0) == 0; }

bool mentionsPosition (const std::string& address)
{
    return address.find ("azimuth") != std::string::npos || address.find ("elevation") != std::string::npos;
}
}

// Forwarding

TEST (EngineWords, ForwardedKeysKeepTheirAddressAndNumber)
{
    for (const auto& key : forwarded())
    {
        const Fields fields = startsWith (key, "channel.") ? Fields { { "ch", 2 } } : Fields {};
        EXPECT_EQ (forwardMessages (truth(), key, fields, 0.25), (std::vector<Message> { { truth().address (key, fields), pyFloat (0.25) } })) << key;
    }
}

TEST (EngineWords, UnforwardedKeysSendNothing)
{
    EXPECT_TRUE (forwardMessages (truth(), "channel.stem.turn", { { "ch", 1 } }, 1).empty());
    EXPECT_TRUE (forwardMessages (truth(), "channel.cue", { { "ch", 1 } }, 1).empty());
}

TEST (EngineWords, TheValueGoesAsAFloat)
{
    const auto messages = forwardMessages (truth(), "master.volume", {}, 1);
    ASSERT_EQ (messages.size(), 1u);
    EXPECT_EQ (messages[0].second.tag, 'f');
    EXPECT_EQ (messages[0].second, pyFloat (1.0));
}

// Words

TEST (EngineWords, Flags)
{
    EXPECT_EQ (flagMessage (truth(), EngineFlag::cue, 0, true), (Message { "/channel/1/cue", pyFloat (1.0) }));
    EXPECT_EQ (flagMessage (truth(), EngineFlag::filter, 3, false), (Message { "/channel/4/filter", pyFloat (0.0) }));
}

TEST (EngineWords, TheFxFlagIsTheFilter)
{
    EXPECT_EQ (flagMessage (truth(), EngineFlag::fx, 1, true), (Message { "/channel/2/filter", pyFloat (1.0) }));
}

TEST (EngineWords, FilterModePositionReturnCueTempoOpen)
{
    EXPECT_EQ (filterModeMessage (truth(), 1), (Message { "/filter/mode", pyFloat (1.0) }));
    EXPECT_EQ (positionMessage (truth(), Position::azimuth, 2, 45), (Message { "/channel/3/azimuth", pyFloat (45.0) }));
    EXPECT_EQ (positionMessage (truth(), Position::elevation, 0, -9), (Message { "/channel/1/elevation", pyFloat (-9.0) }));
    EXPECT_EQ (returnCueMessage (truth(), true), (Message { "/aux-return/cue", pyFloat (1.0) }));
    EXPECT_EQ (returnCueMessage (truth(), false), (Message { "/aux-return/cue", pyFloat (0.0) }));
    EXPECT_EQ (tempoMessage (truth(), 124), (Message { "/delay/bpm", pyFloat (124.0) }));
    EXPECT_EQ (openMessage (truth()), (Message { "/engine/open", pyInt (1) }));
}

TEST (EngineWords, OpenIsTheIntegerOne)
{
    const auto message = openMessage (truth());
    EXPECT_EQ (message.second.tag, 'i');
    EXPECT_EQ (message.second, pyInt (1));
}

// Sources

TEST (EngineWords, SourcesComeFromTheDeckSelector)
{
    const auto got = sourceMessages (truth(), selectorWith ({ 0, 2, 0, 1 }), 0);
    EXPECT_EQ (std::vector<Message> (got.begin(), got.begin() + 4),
               (std::vector<Message> { { "/channel/1/source", pyInt (0) }, { "/channel/2/source", pyInt (2) },
                                       { "/channel/3/source", pyInt (0) }, { "/channel/4/source", pyInt (1) } }));
}

TEST (EngineWords, SourceMessagesAreIntegers)
{
    // The engine refuses a fraction where a whole number is due: 'i', never 'f' (Review Focus 2).
    for (const auto& message : sourceMessages (truth(), selectorWith ({ 0, 2, 0, 1 }), 1))
        EXPECT_EQ (message.second.tag, 'i') << message.first;
}

TEST (EngineWords, ReturnSourceNamesADeck)
{
    EXPECT_EQ (sourceMessages (truth(), DeckSelector(), 2).back(), (Message { "/aux-return/source", pyInt (2) }));
    EXPECT_EQ (sourceMessages (truth(), DeckSelector(), 0).back(), (Message { "/aux-return/source", pyInt (0) }));
    EXPECT_EQ (sourceMessages (truth(), DeckSelector(), 0).size(), 5u);
}

TEST (EngineWords, DeckChannelsSendNoPositionsThreeDFreqQToTheEngine)
{
    const auto s = selectorWith ({ 0, 1, 0, 0 });
    for (const char* key : { "channel.3d", "channel.filter.frequency", "channel.filter.q" })
    {
        EXPECT_TRUE (engineChannelMessages (truth(), s, key, { { "ch", 2 } }, 0.5).empty()) << key;
        EXPECT_EQ (engineChannelMessages (truth(), s, key, { { "ch", 1 } }, 0.5).size(), 1u) << key;
    }
    for (const char* key : { "channel.gain", "channel.volume", "channel.aux-send" })
        EXPECT_EQ (engineChannelMessages (truth(), s, key, { { "ch", 2 } }, 0.5).size(), 1u) << key;
}

TEST (EngineWords, MasterWordsPassThroughTheChannelWrapper)
{
    EXPECT_EQ (engineChannelMessages (truth(), DeckSelector(), "master.booth", {}, 0.3),
               (std::vector<Message> { { "/master/booth", pyFloat (0.3) } }));
}

TEST (EngineWords, AnalogAgainSaysWhatTheEnginesAnalogPathMissed)
{
    ChannelInfo info;
    info.threeD = 0.5;
    info.azimuth = 30;
    Relayed relayed;
    relayed.note ("/channel/3/filter/frequency", pyFloat (0.8));
    const auto got = analogAgainMessages (truth(), 2, info, relayed);
    EXPECT_EQ (got, (std::vector<Message> { { "/channel/3/3d", pyFloat (0.5) },
                                            { "/channel/3/filter/frequency", pyFloat (0.8) },
                                            { "/channel/3/azimuth", pyFloat (30.0) } }));
}

// State

TEST (EngineWords, StateCarriesFlagsModeReturnCueSourcesPositionsAndTempo)
{
    auto chans = channels();
    chans[1].toggleCue = true;
    chans[2].toggleFx = true;
    chans[0].azimuth = 30.0;
    chans[0].elevation = 10.0;
    const auto got = stateMessages (truth(), chans, master (FxMode::highPass, true), DeckSelector(), 0, 126.0);
    for (const auto& message : std::vector<Message> {
             { "/channel/2/cue", pyFloat (1.0) }, { "/channel/1/cue", pyFloat (0.0) }, { "/channel/3/filter", pyFloat (1.0) },
             { "/filter/mode", pyFloat (1.0) }, { "/aux-return/cue", pyFloat (1.0) }, { "/channel/1/source", pyInt (0) },
             { "/aux-return/source", pyInt (0) }, { "/channel/1/azimuth", pyFloat (30.0) }, { "/channel/1/elevation", pyFloat (10.0) },
             { "/delay/bpm", pyFloat (126.0) } })
        EXPECT_TRUE (contains (got, message)) << message.first;
}

TEST (EngineWords, UnknownPositionAndTempoAreLeftUnsaid)
{
    for (const auto& message : stateMessages (truth(), channels(), master(), DeckSelector(), 0, std::nullopt))
    {
        EXPECT_FALSE (mentionsPosition (message.first)) << message.first;
        EXPECT_NE (message.first, "/delay/bpm");
    }
}

TEST (EngineWords, DeckChannelsHaveNoPositionsInTheState)
{
    auto chans = channels();
    for (auto& c : chans)
        c.azimuth = c.elevation = 5.0;
    const auto got = stateMessages (truth(), chans, master(), selectorWith ({ 0, 1, 0, 0 }), 0, std::nullopt);
    for (const auto& message : got)
        EXPECT_FALSE (startsWith (message.first, "/channel/2/") && mentionsPosition (message.first)) << message.first;
    EXPECT_TRUE (contains (got, { "/channel/1/azimuth", pyFloat (5.0) }));
}

// Words_in_the_truth

TEST (EngineWords, EveryWordIsInTheTruth)
{
    auto chans = channels();
    for (auto& c : chans)
        c.azimuth = c.elevation = 1.0;
    auto words = stateMessages (truth(), chans, master(), DeckSelector(), 1, 120.0);
    words.push_back (openMessage (truth()));
    for (const auto& word : words)
        EXPECT_TRUE (truth().match (word.first).has_value()) << word.first;
}

namespace
{
std::string stripped (std::string text)
{
    const auto first = text.find_first_not_of (" \t\n");
    const auto last = text.find_last_not_of (" \t\n");
    return first == std::string::npos ? std::string() : text.substr (first, last - first + 1);
}

std::vector<std::string> addressKeys()
{
    std::vector<std::string> keys;
    const auto addresses = truth().data()["addresses"];   // a copy that outlives the loop
    for (const auto& property : addresses.getDynamicObject()->getProperties())
        keys.push_back (property.name.toString().toStdString());
    return keys;
}

// The truth's own list of what the engine's port carries, read as the Python test reads it.
std::set<std::string> carriedKeys()
{
    auto carries = truth().listener ("engine", "osc")["carries"].toString().toStdString();
    carries = carries.substr (carries.find ("--") + 2);
    std::vector<std::string> tokens;
    std::string token;
    for (char c : carries)
    {
        if (c == ',' || c == '/')
        {
            tokens.push_back (stripped (token));
            token.clear();
        }
        else
            token += c;
    }
    tokens.push_back (stripped (token));

    std::set<std::string> found;
    for (const auto& wanted : tokens)
    {
        if (wanted.empty())
            continue;
        auto trimmed = wanted;
        trimmed.erase (trimmed.find_last_not_of (".*") + 1);
        for (const auto& key : addressKeys())
        {
            const auto tail = (startsWith (key, "channel.") || startsWith (key, "master.")) ? key.substr (key.find ('.') + 1) : key;
            if (wanted == key || wanted == tail || trimmed == key)
                found.insert (key);
        }
    }
    return found;
}
}

TEST (EngineWords, EveryEngineInputIsReplayedOrInTheState)
{
    auto chans = channels();
    for (auto& c : chans)
        c.azimuth = c.elevation = 1.0;
    std::set<std::string> state;
    for (const auto& message : stateMessages (truth(), chans, master(), DeckSelector(), 1, 120.0))
        state.insert (truth().match (message.first)->first);

    auto* values = new juce::DynamicObject();
    for (const auto& key : addressKeys())
        if (truth().addressTruthy (key, "relayed_to"))
            values->setProperty (juce::Identifier (truth().address (key, startsWith (key, "channel.") ? Fields { { "ch", 1 } } : Fields {})), 0.5);
    auto* evening = new juce::DynamicObject();
    evening->setProperty ("values", juce::var (values));
    std::set<std::string> replayed;
    for (const auto& message : replayable (truth(), juce::var (evening)))
        replayed.insert (truth().match (message.first)->first);

    const auto carried = carriedKeys();
    for (const char* needed : { "channel.cue", "channel.source", "delay.bpm" })
        EXPECT_TRUE (carried.count (needed)) << needed;
    for (const auto& key : carried)
    {
        if (key == "engine.open" || key == "channel.3d")   // channel.3d is Motion's live word, never replayed or stated
            continue;
        EXPECT_TRUE (replayed.count (key) || state.count (key)) << key;
    }
}

// Session

TEST (EngineWords, ANewBootIdAsksForAReplayAKnownOneDoesNot)
{
    EngineSession s ({}, [] { return Evening(); });
    EXPECT_TRUE (s.heardHello (pyStr ("a")));
    EXPECT_EQ (s.round, 1);
    EXPECT_FALSE (s.heardHello (pyStr ("a")));
    EXPECT_EQ (s.round, 1);
    EXPECT_TRUE (s.heardHello (pyStr ("b")));
    EXPECT_EQ (s.round, 2);
}

TEST (EngineWords, CoreRestartReplaysIntoAnOpenEngine)
{
    // A fresh session has heard nobody: the first hello, whatever its id, is a replay.
    for (const auto& bootId : { pyStr ("x"), pyInt (0), pyInt (12345) })
        EXPECT_TRUE (EngineSession ({}, [] { return Evening(); }).heardHello (bootId));
}

TEST (EngineWords, EveningIsTheFilesUntilTheFirstReplayThenCoreS)
{
    const Evening file { { "/file", pyInt (1) } };
    const Evening live { { "/live", pyInt (2) } };
    EngineSession s (file, [&] { return live; });
    EXPECT_FALSE (s.live);
    EXPECT_EQ (s.evening(), file);
    s.heardHello (pyStr ("a"));
    EXPECT_EQ (s.evening(), file);
    EXPECT_TRUE (s.replayed (s.round));
    EXPECT_TRUE (s.live);
    EXPECT_EQ (s.evening(), live);
}

TEST (EngineWords, AReplayOvertakenByANewHelloDoesNotCount)
{
    const Evening file { { "/file", pyInt (1) } };
    EngineSession s (file, [] { return Evening { { "/live", pyInt (2) } }; });
    s.heardHello (pyStr ("a"));
    const auto started = s.round;
    s.heardHello (pyStr ("b"));
    EXPECT_FALSE (s.replayed (started));
    EXPECT_FALSE (s.live);
    EXPECT_EQ (s.evening(), file);
}

// TheReturnSourceFollowsTheDesksMode

TEST (EngineWords, StemModeReturnsTheDelay)
{
    EXPECT_EQ (returnDelay, 1);
    EXPECT_EQ (engineReturnSource (Stems::stemMode), returnDelay);
}

TEST (EngineWords, AnalogModeReturnsAnalog)
{
    EXPECT_EQ (engineReturnSource (Stems::analogMode), 0);
}
