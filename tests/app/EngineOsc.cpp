// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <gtest/gtest.h>
#include "AppTruth.h"

namespace
{
using a3apptest::engineTruth;

a3::EngineConfig fourChannelsTwoDecks()
{
    a3::EngineConfig config;
    config.channels = 4;
    config.decks = 2;
    config.analogPairs = { 1, 2, 3, 4 };
    return config;
}

// engine.osc's carries, "a3-engine: Core's words in engine mode -- channel.gain/eq.*/volume, master.*, ...",
// expanded to truth keys: each comma item names its first key in full and the rest after the
// first word ("channel."); a '*' stands for any one word.
std::vector<juce::String> carriedKeys (const juce::var& root)
{
    juce::String carries;
    const auto listeners = root["listeners"];
    for (int i = 0; i < listeners.size(); ++i)
        if (listeners[i]["name"].toString() == "engine.osc")
            carries = listeners[i]["carries"].toString();
    std::vector<juce::String> allKeys;
    if (auto* addresses = root["addresses"].getDynamicObject())
        for (const auto& property : addresses->getProperties())
            allKeys.push_back (property.name.toString());

    std::vector<juce::String> keys;
    for (auto item : juce::StringArray::fromTokens (carries.fromFirstOccurrenceOf ("-- ", false, false), ",", ""))
    {
        auto parts = juce::StringArray::fromTokens (item.trim(), "/", "");
        const auto head = parts[0].upToFirstOccurrenceOf (".", true, false);
        for (int p = 0; p < parts.size(); ++p)
        {
            const auto key = p == 0 ? parts[p] : head + parts[p];
            if (! key.endsWith ("*"))
            {
                keys.push_back (key);
                continue;
            }
            const auto stem = key.dropLastCharacters (1);
            for (const auto& candidate : allKeys)
                if (candidate.startsWith (stem) && ! candidate.substring (stem.length()).containsChar ('.'))
                    keys.push_back (candidate);
        }
    }
    return keys;
}

juce::OSCMessage message (const juce::String& address, juce::OSCArgument argument)
{
    juce::OSCMessage m { juce::OSCAddressPattern (address) };
    m.addArgument (argument);
    return m;
}

float floatOf (const std::atomic<float>& value) { return value.load(); }

// What each carried key changes, read back from the controls (channel 2, index 1).
using Reader = std::function<float (a3::EngineControls&)>;
std::map<juce::String, Reader> readers()
{
    auto ch = [] (std::atomic<float> a3::ChannelControls::* member) -> Reader
    { return [member] (a3::EngineControls& c) { return floatOf (c.channel (1).*member); }; };
    auto strip = [] (std::atomic<float> a3::StripControls::* member) -> Reader
    { return [member] (a3::EngineControls& c) { return floatOf (c.channel (1).*member); }; };
    auto global = [] (std::atomic<float> a3::GlobalControls::* member) -> Reader
    { return [member] (a3::EngineControls& c) { return floatOf (c.global().*member); }; };
    return {
        { "channel.gain", strip (&a3::StripControls::gain) },
        { "channel.eq.high", strip (&a3::StripControls::eqHigh) },
        { "channel.eq.mid", strip (&a3::StripControls::eqMid) },
        { "channel.eq.low", strip (&a3::StripControls::eqLow) },
        { "channel.volume", strip (&a3::StripControls::volume) },
        { "channel.3d", strip (&a3::StripControls::threeD) },
        { "channel.filter.frequency", strip (&a3::StripControls::frequency) },
        { "channel.filter.q", strip (&a3::StripControls::q) },
        { "channel.filter", strip (&a3::StripControls::filterOn) },
        { "channel.aux-send", ch (&a3::ChannelControls::auxSend) },
        { "channel.cue", ch (&a3::ChannelControls::cue) },
        { "channel.azimuth", ch (&a3::ChannelControls::azimuth) },
        { "channel.elevation", ch (&a3::ChannelControls::elevation) },
        { "channel.source", [] (a3::EngineControls& c) { return (float) c.channel (1).source.load(); } },
        { "filter.frequency", [] (a3::EngineControls& c) { return floatOf (c.global().filter.frequency); } },
        { "filter.resonance", [] (a3::EngineControls& c) { return floatOf (c.global().filter.resonance); } },
        { "filter.mode", [] (a3::EngineControls& c) { return (float) c.global().filter.mode.load(); } },
        { "master.volume", global (&a3::GlobalControls::masterVolume) },
        { "master.booth", global (&a3::GlobalControls::booth) },
        { "master.phones-mix", global (&a3::GlobalControls::phonesMix) },
        { "master.phones-volume", global (&a3::GlobalControls::phonesVolume) },
        { "master.aux-return", global (&a3::GlobalControls::auxReturn) },
        { "aux-return.cue", global (&a3::GlobalControls::returnCue) },
        { "delay.bpm", global (&a3::GlobalControls::delayBpm) },
        { "aux-return.delay.beat", global (&a3::GlobalControls::delayBeat) },
        { "aux-return.delay.feedback", global (&a3::GlobalControls::delayFeedback) },
        { "aux-return.delay.color", global (&a3::GlobalControls::delayColor) },
        { "aux-return.delay.spread", global (&a3::GlobalControls::delaySpread) },
        { "engine.open", [] (a3::EngineControls& c) { return c.global().gateOpen.load() ? 1.0f : 0.0f; } },
    };
}

// A 'f' word gets 0.25, an 'i' word 1; what each should read back as.
float expectedAfter (const juce::String& key, bool integer)
{
    if (integer)
        return 1.0f;
    if (key == "delay.bpm")
        return 20.0f;   // 0.25 clamped to 20..400
    if (key == "filter.mode")
        return 0.0f;    // 0.25 is not above 0.5: low-pass
    return 0.25f;
}
}

namespace
{
struct EngineOsc : ::testing::Test
{
    a3app::EngineTruth truth = engineTruth();
    a3::EngineConfig config = fourChannelsTwoDecks();
    a3::EngineControls controls { 4 };

    bool apply (const juce::OSCMessage& m) { return a3app::applyEngineMessage (truth, config, m, controls); }
    bool apply (const char* address, float value) { return apply (message (address, juce::OSCArgument (value))); }
    bool applyInt (const char* address, int value) { return apply (message (address, juce::OSCArgument ((juce::int32) value))); }
};
}

TEST_F (EngineOsc, EveryCarriedKeyIsUnderstood)
{
    const auto root = juce::JSON::parse (a3apptest::truthText());
    const auto keys = carriedKeys (root);
    ASSERT_EQ (keys.size(), 29u);

    auto sorted = keys;
    auto vocabulary = a3app::engineWordKeys();
    std::sort (sorted.begin(), sorted.end());
    std::sort (vocabulary.begin(), vocabulary.end());
    EXPECT_EQ (sorted, vocabulary) << "the engine carries exactly what engine.osc's carries names";

    const auto truth = engineTruth();
    const auto config = fourChannelsTwoDecks();
    const auto read = readers();
    for (const auto& key : keys)
    {
        const auto entry = root["addresses"][juce::Identifier (key)];
        const auto address = entry["pattern"].toString().replace ("{ch}", "2");
        const bool integer = entry["args"].toString() == "i";
        a3::EngineControls controls (4);
        const auto m = integer ? message (address, juce::OSCArgument ((juce::int32) 1))
                               : message (address, juce::OSCArgument (0.25f));
        EXPECT_TRUE (a3app::applyEngineMessage (truth, config, m, controls)) << key;
        ASSERT_EQ (read.count (key), 1u) << key;
        EXPECT_FLOAT_EQ (read.at (key) (controls), expectedAfter (key, integer)) << key;
    }
}

TEST_F (EngineOsc, ReadsCoresAddressesAndTheHello)
{
    const auto truth = engineTruth();
    EXPECT_EQ (truth.corePort, 9000);
    EXPECT_EQ (truth.coreHost, "127.0.0.1");   // core.osc listens on "any": reached on loopback
    EXPECT_EQ (truth.helloAddress, "/engine/hello");
    EXPECT_EQ (truth.enginePort, 9004);

    juce::String error;
    auto text = a3apptest::truthText();
    EXPECT_FALSE (a3app::parseEngineTruth (text.replace ("\"delay.bpm\"", "\"delay.other\""), error).has_value());
    EXPECT_TRUE (error.contains ("delay.bpm")) << error;
    EXPECT_FALSE (a3app::parseEngineTruth ("not json", error).has_value());
    EXPECT_FALSE (error.isEmpty());
}

// A8: the deck slots of a config that names no count are the truth's deck range (stemdeck.deck-hello),
// whatever its upper bound; a truth without that range is refused, naming it.
TEST_F (EngineOsc, TheDeckCountIsTheTruthsDeckRange)
{
    EXPECT_EQ (engineTruth().decks, 4);
    juce::String error;
    const auto wider = a3app::parseEngineTruth (a3apptest::truthWithDeckRange (6), error);
    ASSERT_TRUE (wider.has_value()) << error;
    EXPECT_EQ (wider->decks, 6);
    EXPECT_FALSE (a3app::parseEngineTruth (a3apptest::truthText().replace ("\"stemdeck.deck-hello\"", "\"stemdeck.other\""), error));
    EXPECT_TRUE (error.contains ("stemdeck.deck-hello")) << error;
}

// The /vu block names deck_a ... deck_d: slots 1-4 have a meter, a fifth has none, and that is said.
TEST_F (EngineOsc, ADeckSlotBeyondTheMetersIsSaid)
{
    const auto& meters = engineTruth().vuMeters;
    EXPECT_TRUE (a3app::unmeteredDecksNote (4, meters).isEmpty());
    EXPECT_TRUE (a3app::unmeteredDecksNote (0, meters).isEmpty());
    const auto five = a3app::unmeteredDecksNote (5, meters);
    EXPECT_TRUE (five.contains ("deck E")) << five;
    EXPECT_FALSE (five.contains ("deck D")) << five;
    const auto six = a3app::unmeteredDecksNote (6, meters);
    EXPECT_TRUE (six.contains ("E") && six.contains ("F")) << six;
    auto more = meters;
    more.push_back ("deck_e");   // a truth that grew a meter for E: the engine needs no change
    EXPECT_TRUE (a3app::unmeteredDecksNote (5, more).isEmpty());
}

// A saved engine.json may name fewer decks than the truth has (the rig's "decks": 2): the slots it
// hides would look selectable on the desk and stay silent, so that is said, with what to change.
TEST_F (EngineOsc, ASavedDeckCountBelowTheTruthIsSaid)
{
    const auto two = a3app::hiddenDecksNote (2, true, 4);
    EXPECT_EQ (two, "engine.json names 2 decks, the truth 4: decks C, D have no slot; set Decks to 4 on the CHANNELS page "
                   "of the a3-core window, or remove the \"decks\" line from engine.json");
    const auto three = a3app::hiddenDecksNote (3, true, 4);
    EXPECT_TRUE (three.contains ("deck D has no slot")) << three;
    EXPECT_TRUE (a3app::hiddenDecksNote (4, true, 4).isEmpty());
    EXPECT_TRUE (a3app::hiddenDecksNote (6, true, 4).isEmpty());   // more than the truth: his choice, said elsewhere
    EXPECT_TRUE (a3app::hiddenDecksNote (4, false, 4).isEmpty());  // no count in the file: the truth's
}

TEST_F (EngineOsc, ATruthWithoutDeckAudioStillParsesAndSaysSo)
{
    EXPECT_EQ (engineTruth().deckAudioPattern, "/engine/deck/{deck}/audio");
    EXPECT_TRUE (a3app::truthNote (engineTruth()).isEmpty());

    juce::String error;
    const auto old = a3app::parseEngineTruth (a3apptest::truthText().replace ("\"engine.deck-audio\"", "\"engine.deck-other\""), error);
    ASSERT_TRUE (old.has_value()) << error;
    EXPECT_TRUE (old->deckAudioPattern.isEmpty());
    EXPECT_TRUE (a3app::truthNote (*old).contains ("engine.deck-audio"));
}

// The brief's list in one place; the tests after it go through each kind in turn.
TEST_F (EngineOsc, GarbageIsRefusedOrClamped)
{
    EXPECT_FALSE (apply (message ("/channel/1/gain", juce::OSCArgument (juce::String ("0.9")))));
    EXPECT_FALSE (applyInt ("/channel/1/source", 7));
    EXPECT_FALSE (applyInt ("/channel/1/source", 3));
    EXPECT_FALSE (apply ("/channel/0/gain", 0.9f));
    EXPECT_FALSE (apply ("/channel/5/gain", 0.9f));
    EXPECT_TRUE (apply ("/delay/bpm", 0.0f));
    EXPECT_EQ (controls.global().delayBpm.load(), 20.0f);
    EXPECT_TRUE (apply ("/delay/bpm", 1.0e6f));
    EXPECT_EQ (controls.global().delayBpm.load(), 400.0f);

    controls.channel (0).gain = 0.5f;
    controls.channel (0).azimuth = 10.0f;
    controls.global().delayBpm = 120.0f;
    for (const float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity() })
    {
        EXPECT_FALSE (apply ("/channel/1/gain", bad));
        EXPECT_FALSE (apply ("/channel/1/azimuth", bad));
        EXPECT_FALSE (apply ("/delay/bpm", bad));
        EXPECT_FALSE (apply ("/filter/mode", bad));
        EXPECT_FALSE (apply ("/channel/1/source", bad));
    }
    EXPECT_EQ (controls.channel (0).gain.load(), 0.5f);
    EXPECT_EQ (controls.channel (0).azimuth.load(), 10.0f);
    EXPECT_EQ (controls.global().delayBpm.load(), 120.0f);
    EXPECT_EQ (controls.channel (0).source.load(), 0);
}

TEST_F (EngineOsc, StringsAndWrongArgumentCountsAreRefused)
{
    controls.channel (0).gain = 0.5f;
    EXPECT_FALSE (apply (message ("/channel/1/gain", juce::OSCArgument (juce::String ("0.9")))));
    EXPECT_FALSE (apply (message ("/channel/1/source", juce::OSCArgument (juce::String ("1")))));
    EXPECT_FALSE (apply (message ("/engine/open", juce::OSCArgument (juce::String ("1")))));
    EXPECT_FALSE (apply (juce::OSCMessage { juce::OSCAddressPattern ("/channel/1/gain") }));
    juce::OSCMessage two { juce::OSCAddressPattern ("/channel/1/gain") };
    two.addFloat32 (0.9f);
    two.addFloat32 (0.9f);
    EXPECT_FALSE (apply (two));
    EXPECT_EQ (controls.channel (0).gain.load(), 0.5f);
    EXPECT_FALSE (controls.global().gateOpen.load());
}

TEST_F (EngineOsc, SourcesOutsideTheDecksAreRefused)
{
    EXPECT_TRUE (applyInt ("/channel/1/source", 2));
    EXPECT_FALSE (applyInt ("/channel/1/source", 7));
    EXPECT_FALSE (applyInt ("/channel/1/source", 3));   // two decks
    EXPECT_FALSE (applyInt ("/channel/1/source", -1));
    EXPECT_FALSE (apply ("/channel/1/source", 1.5f));
    EXPECT_EQ (controls.channel (0).source.load(), 2);
    EXPECT_TRUE (apply ("/channel/1/source", 1.0f));    // a whole number as a float is still the number
    EXPECT_EQ (controls.channel (0).source.load(), 1);

}

// P45: the return has no source any more; the old word is not the engine's.
TEST_F (EngineOsc, TheReturnSourceIsNoWordAnyMore)
{
    EXPECT_FALSE (applyInt ("/aux-return/source", 1));
}

TEST_F (EngineOsc, AnalogIsRefusedOnADeckOnlyChannel)
{
    config.analogPairs = { 1, 2, 3, 0 };
    EXPECT_TRUE (applyInt ("/channel/4/source", 1));
    EXPECT_FALSE (applyInt ("/channel/4/source", 0));
    EXPECT_EQ (controls.channel (3).source.load(), 1);
    EXPECT_TRUE (applyInt ("/channel/1/source", 0));
}

TEST_F (EngineOsc, ChannelsOutsideTheTruthAndTheEngineAreRefused)
{
    EXPECT_FALSE (apply ("/channel/0/gain", 0.9f));
    EXPECT_FALSE (apply ("/channel/5/gain", 0.9f));
    EXPECT_FALSE (apply ("/channel/x/gain", 0.9f));
    EXPECT_FALSE (apply ("/channel/01x/gain", 0.9f));

    config.channels = 3;
    a3::EngineControls three (3);
    EXPECT_FALSE (a3app::applyEngineMessage (truth, config, message ("/channel/4/gain", juce::OSCArgument (0.9f)), three));
    EXPECT_TRUE (a3app::applyEngineMessage (truth, config, message ("/channel/3/gain", juce::OSCArgument (0.9f)), three));
}

TEST_F (EngineOsc, WordsTheEngineDoesNotCarryAreRefused)
{
    for (const char* address : { "/channel/1/stem/turn", "/channel/1/source/cursor", "/channel/1/cue/led",
                                 "/filter/led", "/engine/hello", "/vu/1", "/channel/1/gain/extra" })
        EXPECT_FALSE (apply (address, 1.0f)) << address;
}

TEST_F (EngineOsc, ValuesAreClampedAsCoreClampsThem)
{
    EXPECT_TRUE (apply ("/channel/1/gain", 3.0f));
    EXPECT_EQ (controls.channel (0).gain.load(), 1.0f);
    EXPECT_TRUE (apply ("/channel/1/volume", -2.0f));
    EXPECT_EQ (controls.channel (0).volume.load(), 0.0f);
    EXPECT_TRUE (apply ("/master/volume", 7.0f));
    EXPECT_EQ (controls.global().masterVolume.load(), 1.0f);
    EXPECT_TRUE (applyInt ("/channel/2/cue", 5));
    EXPECT_EQ (controls.channel (1).cue.load(), 1.0f);

    EXPECT_TRUE (apply ("/channel/1/azimuth", 200.0f));
    EXPECT_EQ (controls.channel (0).azimuth.load(), 180.0f);
    EXPECT_TRUE (apply ("/channel/1/azimuth", -200.0f));
    EXPECT_EQ (controls.channel (0).azimuth.load(), -180.0f);
    EXPECT_TRUE (apply ("/channel/1/elevation", 100.0f));
    EXPECT_EQ (controls.channel (0).elevation.load(), 90.0f);

    EXPECT_TRUE (apply ("/delay/bpm", 0.0f));
    EXPECT_EQ (controls.global().delayBpm.load(), 20.0f);
    EXPECT_TRUE (apply ("/delay/bpm", 1.0e6f));
    EXPECT_EQ (controls.global().delayBpm.load(), 400.0f);
    EXPECT_TRUE (applyInt ("/delay/bpm", 128));
    EXPECT_EQ (controls.global().delayBpm.load(), 128.0f);
}

// The delay page's values on their legal sets: a beat to the nearest legal one in log, the rest
// clamped; an int is read as the number it is.
TEST_F (EngineOsc, TheDelayValuesAreSnappedAndClamped)
{
    auto& g = controls.global();
    EXPECT_FLOAT_EQ (g.delayBeat.load(), 0.25f);
    EXPECT_FLOAT_EQ (g.delayFeedback.load(), 0.70f);
    EXPECT_FLOAT_EQ (g.delayColor.load(), 0.0f);
    EXPECT_FLOAT_EQ (g.delaySpread.load(), 60.0f);

    const std::pair<float, float> beats[] { { 0.3f, 0.25f }, { 0.6f, 0.5f }, { 0.62f, 0.75f }, { 3.0f, 4.0f },
                                            { 100.0f, 4.0f }, { 0.0f, 0.125f }, { -1.0f, 0.125f } };
    for (const auto& [sent, stored] : beats)
    {
        EXPECT_TRUE (apply ("/aux-return/delay/beat", sent));
        EXPECT_FLOAT_EQ (g.delayBeat.load(), stored) << sent;
    }
    EXPECT_TRUE (applyInt ("/aux-return/delay/beat", 2));
    EXPECT_FLOAT_EQ (g.delayBeat.load(), 2.0f);

    EXPECT_TRUE (apply ("/aux-return/delay/feedback", 2.0f));
    EXPECT_FLOAT_EQ (g.delayFeedback.load(), 0.95f);
    EXPECT_TRUE (apply ("/aux-return/delay/feedback", -1.0f));
    EXPECT_FLOAT_EQ (g.delayFeedback.load(), 0.0f);
    EXPECT_TRUE (apply ("/aux-return/delay/color", 3.0f));
    EXPECT_FLOAT_EQ (g.delayColor.load(), 1.0f);
    EXPECT_TRUE (apply ("/aux-return/delay/color", -0.4f));
    EXPECT_FLOAT_EQ (g.delayColor.load(), -0.4f);
    EXPECT_TRUE (apply ("/aux-return/delay/spread", 270.0f));
    EXPECT_FLOAT_EQ (g.delaySpread.load(), 180.0f);
    EXPECT_TRUE (applyInt ("/aux-return/delay/spread", 90));
    EXPECT_FLOAT_EQ (g.delaySpread.load(), 90.0f);

    for (const char* address : { "/aux-return/delay/beat", "/aux-return/delay/feedback", "/aux-return/delay/color",
                                 "/aux-return/delay/spread" })
    {
        EXPECT_FALSE (apply (address, std::numeric_limits<float>::quiet_NaN())) << address;
        EXPECT_FALSE (apply (message (address, juce::OSCArgument (juce::String ("1"))))) << address;
    }
    EXPECT_FLOAT_EQ (g.delaySpread.load(), 90.0f);
}

TEST_F (EngineOsc, FilterModeTakesANumberOrCoresWord)
{
    EXPECT_TRUE (apply ("/filter/mode", 0.0f));
    EXPECT_EQ (controls.global().filter.mode.load(), 0);
    EXPECT_TRUE (applyInt ("/filter/mode", 1));
    EXPECT_EQ (controls.global().filter.mode.load(), 1);
    EXPECT_TRUE (apply (message ("/filter/mode", juce::OSCArgument (juce::String ("low_pass")))));
    EXPECT_EQ (controls.global().filter.mode.load(), 0);
    EXPECT_TRUE (apply (message ("/filter/mode", juce::OSCArgument (juce::String ("high_pass")))));
    EXPECT_EQ (controls.global().filter.mode.load(), 1);
    EXPECT_FALSE (apply (message ("/filter/mode", juce::OSCArgument (juce::String ("sideways")))));
    EXPECT_EQ (controls.global().filter.mode.load(), 1);
}

TEST_F (EngineOsc, OnlyOpenOpens)
{
    EXPECT_FALSE (applyInt ("/engine/open", 2));
    EXPECT_FALSE (applyInt ("/engine/open", -1));
    EXPECT_FALSE (apply ("/engine/open", 0.5f));
    EXPECT_FALSE (apply ("/engine/open", std::numeric_limits<float>::quiet_NaN()));
    EXPECT_TRUE (apply ("/channel/1/gain", 1.0f));
    EXPECT_TRUE (applyInt ("/aux-return/cue", 1));
    EXPECT_FALSE (controls.global().gateOpen.load());

    EXPECT_TRUE (applyInt ("/engine/open", 1));
    EXPECT_TRUE (controls.global().gateOpen.load());
    EXPECT_FALSE (applyInt ("/engine/open", 2));
    EXPECT_TRUE (controls.global().gateOpen.load()) << "a refused open changes nothing";
    EXPECT_TRUE (applyInt ("/engine/open", 0));
    EXPECT_FALSE (controls.global().gateOpen.load());
    EXPECT_TRUE (apply ("/engine/open", 1.0f));
    EXPECT_TRUE (controls.global().gateOpen.load());
}
