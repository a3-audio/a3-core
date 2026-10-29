// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "AllRadDesign.h"
#include "SpatialTestData.h"

namespace
{
juce::var shippedMainDecoder()
{
    return juce::JSON::parse (a3test::shippedSpatialDirectory.getChildFile ("main.decoder.json").loadFileAsString());
}

std::vector<a3::Loudspeaker> loudspeakersOf (const juce::var& file)
{
    std::vector<a3::Loudspeaker> speakers;
    const auto& rows = file["LoudspeakerLayout"]["Loudspeakers"];
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        speakers.push_back ({ (float) row["Azimuth"], (float) row["Elevation"], (float) row["Radius"],
                              (bool) row["IsImaginary"], (int) row["Channel"], (float) row["Gain"] });
    }
    return speakers;
}

std::vector<a3::Loudspeaker> quadOnly()
{
    return { { 45, 0 }, { -45, 0, 1, false, 2 }, { -135, 0, 1, false, 3 }, { 135, 0, 1, false, 4 } };
}
}

TEST (AllRadDesign, TheShippedQuadReproducesItsMatrix)
{
    const auto shipped = shippedMainDecoder();
    juce::String error;
    const auto designed = a3::designDecoder (loudspeakersOf (shipped), a3::AmbisonicWeights::inPhase, error);
    ASSERT_TRUE (designed.has_value()) << error;

    const auto ours = juce::JSON::parse (*designed);
    const auto& expected = shipped["Decoder"]["Matrix"];
    const auto& actual = ours["Decoder"]["Matrix"];
    ASSERT_EQ (actual.size(), expected.size());
    for (int row = 0; row < expected.size(); ++row)
    {
        ASSERT_EQ (actual[row].size(), 16) << "order 3";
        for (int column = 0; column < 16; ++column)
            EXPECT_NEAR ((double) actual[row][column], (double) expected[row][column], 1e-5) << row << "," << column;
    }
    EXPECT_EQ (juce::JSON::toString (ours["Decoder"]["Routing"], true), juce::JSON::toString (shipped["Decoder"]["Routing"], true));
    EXPECT_EQ (ours["Decoder"]["Weights"].toString(), "inPhase");
    EXPECT_EQ (ours["LoudspeakerLayout"]["Loudspeakers"].size(), 6);
}

TEST (AllRadDesign, ABrokenLayoutReturnsIemsMessage)
{
    juce::String error;
    const std::vector<a3::Loudspeaker> three { { 0, 0 }, { 120, 0, 1, false, 2 }, { -120, 0, 1, false, 3 } };
    EXPECT_FALSE (a3::designDecoder (three, a3::AmbisonicWeights::inPhase, error).has_value());
    EXPECT_TRUE (error.contains ("ERROR 2: There are less than 4 loudspeakers")) << error;
}

TEST (AllRadDesign, AFlatQuadWithoutImaginaryPointsIsRefusedByIem)
{
    juce::String error;
    EXPECT_FALSE (a3::designDecoder (quadOnly(), a3::AmbisonicWeights::maxrE, error).has_value());
    EXPECT_TRUE (error.startsWith ("ERROR")) << error;
}

TEST (AllRadDesign, TheWeightsAreWhatTheFileSays)
{
    auto speakers = loudspeakersOf (shippedMainDecoder());
    for (const auto& [weights, name] : { std::pair { a3::AmbisonicWeights::basic, "none" },
                                         std::pair { a3::AmbisonicWeights::maxrE, "maxrE" } })
    {
        juce::String error;
        const auto designed = a3::designDecoder (speakers, weights, error);
        ASSERT_TRUE (designed.has_value()) << error;
        EXPECT_EQ (juce::JSON::parse (*designed)["Decoder"]["Weights"].toString(), name);
    }
}

TEST (AllRadDesign, LoadTakesAnAllRadFileAndRefusesABrokenOne)
{
    a3::AllRadDesigner designer;
    EXPECT_TRUE (designer.load (juce::JSON::toString (shippedMainDecoder())).wasOk());
    juce::String error;
    const auto designed = designer.design (error);
    ASSERT_TRUE (designed.has_value()) << error;
    EXPECT_EQ (juce::JSON::parse (*designed)["Decoder"]["Weights"].toString(), "inPhase");

    const auto refused = designer.load (R"({"LoudspeakerLayout":{"Loudspeakers":[{"Azimuth":0}]}})");
    EXPECT_TRUE (refused.failed());
    EXPECT_TRUE (refused.getErrorMessage().contains ("Elevation")) << refused.getErrorMessage();
}
