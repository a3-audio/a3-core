// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "EngineConfig.h"

namespace
{
juce::var section (const juce::String& json)
{
    return juce::JSON::parse (json);
}

std::optional<a3::EngineConfig> parse (const juce::String& json, juce::String& error)
{
    return a3::parseEngineConfig (section (json), error);
}

void expectRefused (const juce::String& json, const juce::String& mention)
{
    juce::String error;
    EXPECT_FALSE (parse (json, error).has_value()) << json;
    EXPECT_TRUE (error.containsIgnoreCase (mention)) << json << " -> '" << error << "'";
}
}

TEST (EngineConfig, DefaultsAreFourChannelsSvf)
{
    juce::String error;
    const auto c = parse ("{}", error);
    ASSERT_TRUE (c.has_value()) << error;
    EXPECT_EQ (c->channels, 4);
    EXPECT_EQ (c->analogInputs, 12);
    EXPECT_EQ (c->analogPairs, (std::vector<int> { 1, 2, 3, 4 }));
    EXPECT_EQ (c->returnAnalogPair, 6);
    EXPECT_EQ (c->filterKind, a3::FilterKind::svf);
    EXPECT_FALSE (c->trims.main.has_value());
    EXPECT_FALSE (c->trims.booth.has_value());
    EXPECT_FALSE (c->trims.phones.has_value());
    EXPECT_EQ (c->trims.rec, 0.0f);
    ASSERT_EQ (c->delayModulation.size(), 2u);
    EXPECT_EQ (c->delayModulation[0].parameter, "pitchL");
    EXPECT_EQ (c->delayModulation[0].base, 0.0f);
    EXPECT_EQ (c->delayModulation[0].depth, 18.0f);
    EXPECT_EQ (c->delayModulation[0].rateHz, 0.124573f);
    EXPECT_EQ (c->delayModulation[0].shape, "random");
    EXPECT_EQ (c->delayModulation[1].parameter, "pitchR");
    EXPECT_EQ (c->delayModulation[1].rateHz, 0.018817f);
    EXPECT_EQ (*c, a3::EngineConfig {});   // the struct's defaults are the parsed defaults
}

TEST (EngineConfig, DefaultsAreTwoDecks)
{
    juce::String error;
    const auto c = parse ("{}", error);
    ASSERT_TRUE (c.has_value());
    EXPECT_EQ (c->decks, 2);
}

TEST (EngineConfig, RoundTrips)
{
    a3::EngineConfig c;
    c.channels = 6;
    c.decks = 3;
    c.analogInputs = 16;
    c.analogPairs = { 1, 0, 3, 4, 0, 7 };
    c.returnAnalogPair = 8;
    c.trims.main = -12.5f;
    c.trims.booth = 0.0f;
    c.trims.phones = -3.25f;
    c.trims.rec = 1.5f;
    c.filterKind = a3::FilterKind::ladder;
    c.delayModulation = { { "pitchL", 1.0f, 2.0f, 0.5f, "sine" }, { "x", -1.0f, 3.0f, 2.0f, "triangle" } };

    juce::String error;
    const auto text = juce::JSON::toString (a3::toJson (c));   // through real JSON text
    const auto back = parse (text, error);
    ASSERT_TRUE (back.has_value()) << error;
    EXPECT_EQ (*back, c);

    const auto defaults = parse (juce::JSON::toString (a3::toJson (a3::EngineConfig {})), error);
    ASSERT_TRUE (defaults.has_value()) << error;
    EXPECT_EQ (*defaults, a3::EngineConfig {});
}

TEST (EngineConfig, RefusedChannelsBelowOne)
{
    expectRefused (R"({"channels": 0})", "channels");
    expectRefused (R"({"channels": -1})", "channels");
    expectRefused (R"({"channels": 2.5})", "channels");
    expectRefused (R"({"channels": "four"})", "channels");
}

TEST (EngineConfig, RefusedDecksBelowZero)
{
    expectRefused (R"({"decks": -1})", "decks");
    expectRefused (R"({"decks": 1.5})", "decks");
    expectRefused (R"({"decks": "two"})", "decks");
    juce::String error;
    const auto none = parse (R"({"decks": 0})", error);
    ASSERT_TRUE (none.has_value()) << error;
    EXPECT_EQ (none->decks, 0);
}

TEST (EngineConfig, RefusedPairBeyondInputs)
{
    expectRefused (R"({"analogPairs": [1,2,3,7]})", "analogPairs");      // pair 7 = inputs 13/14 of 12
    expectRefused (R"({"analogPairs": [1,2,3,-1]})", "analogPairs");
    expectRefused (R"({"analogInputs": 8})", "returnAnalogPair");        // return pair 6 beyond 8 inputs
    expectRefused (R"({"returnAnalogPair": 7})", "returnAnalogPair");
    expectRefused (R"({"analogPairs": [1,1,3,4]})", "analogPairs");      // one pair, two channels
    expectRefused (R"({"channels": 2, "analogPairs": [1,2,3]})", "analogPairs");
}

TEST (EngineConfig, RefusedChannelOnTheReturnPair)
{
    expectRefused (R"({"analogPairs": [1,2,3,6]})", "return");
}

TEST (EngineConfig, RefusedUnknownFilterKind)
{
    expectRefused (R"({"filterKind": "moog"})", "filterKind");
    expectRefused (R"({"filterKind": 1})", "filterKind");
    juce::String error;
    const auto ladder = parse (R"({"filterKind": "ladder"})", error);
    ASSERT_TRUE (ladder.has_value()) << error;
    EXPECT_EQ (ladder->filterKind, a3::FilterKind::ladder);
}

TEST (EngineConfig, ZoneWithoutTrimStaysClosed)
{
    juce::String error;
    const auto c = parse (R"({"trimsDb": {"booth": -6.0}})", error);
    ASSERT_TRUE (c.has_value()) << error;
    EXPECT_FALSE (c->trims.main.has_value());
    EXPECT_TRUE (c->trims.booth.has_value());
    EXPECT_EQ (*c->trims.booth, -6.0f);
    EXPECT_FALSE (c->trims.phones.has_value());
    EXPECT_TRUE (error.isEmpty());
}

TEST (EngineConfig, NonFiniteTrimIsRefused)
{
    for (const double bad : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
    {
        auto* trims = new juce::DynamicObject();
        trims->setProperty ("main", bad);
        auto* root = new juce::DynamicObject();
        root->setProperty ("trimsDb", juce::var (trims));
        juce::String error;
        EXPECT_FALSE (a3::parseEngineConfig (juce::var (root), error).has_value());
        EXPECT_TRUE (error.contains ("main")) << error;
    }
    expectRefused (R"({"trimsDb": {"phones": "loud"}})", "phones");
    expectRefused (R"({"trimsDb": {"rec": null}})", "rec");
}

TEST (EngineConfig, LargeNIsAccepted)
{
    juce::String error;
    const auto c = parse (R"({"channels": 64})", error);
    ASSERT_TRUE (c.has_value()) << error;
    EXPECT_EQ (c->channels, 64);
    ASSERT_EQ (c->analogPairs.size(), 64u);
    EXPECT_EQ (c->analogPairs[3], 4);
    EXPECT_EQ (c->analogPairs[4], 0);   // beyond the default four: deck-only
}

TEST (EngineConfig, DeckOnlyChannelsAreAllowed)
{
    juce::String error;
    const auto c = parse (R"({"analogPairs": [1,0,3,0]})", error);
    ASSERT_TRUE (c.has_value()) << error;
    EXPECT_EQ (c->analogPairs, (std::vector<int> { 1, 0, 3, 0 }));
    const auto shorter = parse (R"({"channels": 4, "analogPairs": [2]})", error);
    ASSERT_TRUE (shorter.has_value()) << error;
    EXPECT_EQ (shorter->analogPairs, (std::vector<int> { 2, 0, 0, 0 }));   // absent channels are deck-only
}

TEST (EngineConfig, RefusedBrokenSections)
{
    juce::String error;
    EXPECT_FALSE (a3::parseEngineConfig (juce::var(), error).has_value());   // missing section
    EXPECT_FALSE (error.isEmpty());
    expectRefused ("[1,2]", "object");
    expectRefused (R"({"chanels": 4})", "chanels");                  // a typo must not fall back silently
    expectRefused (R"({"trimsDb": {"mian": -6}})", "mian");
    expectRefused (R"({"trimsDb": 3})", "trimsDb");
}

TEST (EngineConfig, DelayModulationIsValidated)
{
    juce::String error;
    const auto none = parse (R"({"delayModulation": []})", error);
    ASSERT_TRUE (none.has_value()) << error;
    EXPECT_TRUE (none->delayModulation.empty());

    const auto ok = parse (R"({"delayModulation": [
        {"parameter":"a","base":0,"depth":1,"rateHz":0.5,"shape":"sine"},
        {"parameter":"b","base":0,"depth":1,"rateHz":0.5,"shape":"triangle"}]})", error);
    ASSERT_TRUE (ok.has_value()) << error;
    EXPECT_EQ (ok->delayModulation.size(), 2u);

    expectRefused (R"({"delayModulation": [{"parameter":"pitchL","base":0,"depth":1,"rateHz":0.5,"shape":"saw"}]})", "pitchL");
    expectRefused (R"({"delayModulation": [{"parameter":"q","base":0,"depth":1,"rateHz":0.5}]})", "shape");
    expectRefused (R"({"delayModulation": [{"parameter":"q","base":0,"depth":1,"shape":"sine"}]})", "rateHz");
    expectRefused (R"({"delayModulation": [{"parameter":"q","base":0,"depth":1,"rateHz":-1,"shape":"sine"}]})", "rateHz");
    expectRefused (R"({"delayModulation": [{"base":0,"depth":1,"rateHz":1,"shape":"sine"}]})", "parameter");
    expectRefused (R"({"delayModulation": {"parameter":"q"}})", "delayModulation");
}
