// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Includes only the library header: the link map must parse with no engine code in sight.
#include "a3-links/LinkMap.h"
#include <gtest/gtest.h>

namespace
{
const std::vector<std::string> inputs { "analog_1", "deckA_1" };
const std::vector<std::string> outputs { "main_top_1", "rec_L" };

juce::var json (const char* text)
{
    juce::var parsed;
    EXPECT_TRUE (juce::JSON::parse (text, parsed).wasOk());
    return parsed;
}
}

TEST (LinkMap, ParsesWithoutEngineHeaders)
{
    juce::String error;
    const auto map = a3::links::parseLinkMap (
        json (R"({"jack":{"analog_1":["system:capture_1"],"rec_L":["StemDeck-A:rec_in_L","x:y"]},)"
              R"("directLinks":[["system:capture_2","beat-analyzer:in_1"]]})"),
        inputs, outputs, error);
    ASSERT_TRUE (map.has_value()) << error;
    EXPECT_EQ (map->jack.at ("analog_1"), (std::vector<std::string> { "system:capture_1" }));
    EXPECT_EQ (map->jack.at ("rec_L").size(), 2u);
    ASSERT_EQ (map->directLinks.size(), 1u);
    EXPECT_EQ (map->directLinks[0].second, "beat-analyzer:in_1");
}

TEST (LinkMap, RoundTripsThroughJson)
{
    a3::links::LinkMap map;
    map.jack["analog_1"] = { "system:capture_1" };
    map.directLinks = { { "a:out", "b:in" } };
    juce::String error;
    const auto back = a3::links::parseLinkMap (a3::links::toJson (map), inputs, outputs, error);
    ASSERT_TRUE (back.has_value()) << error;
    EXPECT_EQ (back->jack, map.jack);
    EXPECT_EQ (back->directLinks, map.directLinks);
}

TEST (LinkMap, RefusesWhatItCannotUse)
{
    juce::String error;
    EXPECT_FALSE (a3::links::parseLinkMap (json (R"({"jack":{"nope_1":["x:y"]}})"), inputs, outputs, error).has_value());
    EXPECT_TRUE (error.contains ("nope_1")) << error;
    EXPECT_FALSE (a3::links::parseLinkMap (json (R"({"jack":{"rec_L":[3]}})"), inputs, outputs, error).has_value());
    EXPECT_FALSE (a3::links::parseLinkMap (json (R"({"directLinks":[["only-one"]]})"), inputs, outputs, error).has_value());
    EXPECT_FALSE (a3::links::parseLinkMap (json (R"({"mystery":1})"), inputs, outputs, error).has_value());
    EXPECT_TRUE (error.contains ("mystery")) << error;
}
