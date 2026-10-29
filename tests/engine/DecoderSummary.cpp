// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "DecoderSummary.h"
#include "SpatialTestData.h"

namespace
{
juce::String shipped (const char* name) { return a3test::shippedSpatialDirectory.getChildFile (name).loadFileAsString(); }
}

TEST (DecoderSummary, TheShippedMainIsTheQuadOfFebruary)
{
    juce::String error;
    const auto summary = a3::decoderSummary (shipped ("main.decoder.json"), juce::JSON::parse (shipped ("main.parameters.json")), error);
    ASSERT_TRUE (summary.has_value()) << error;
    EXPECT_EQ (summary->madeWith, "IEM AllRADecoder v0.9.5");
    EXPECT_EQ (summary->exportedOn, "28 Feb 2026 3:16:23pm");
    EXPECT_EQ (summary->order, 3);
    EXPECT_EQ (summary->fileWeights, "inPhase");
    EXPECT_EQ (summary->weights, "inPhase");
    ASSERT_EQ (summary->loudspeakers.size(), 6u);
    EXPECT_EQ (summary->loudspeakers[0].channel, 2);
    EXPECT_FLOAT_EQ (summary->loudspeakers[0].azimuth, 45.0f);
    EXPECT_FALSE (summary->loudspeakers[3].imaginary);
    EXPECT_TRUE (summary->loudspeakers[4].imaginary);
    EXPECT_FLOAT_EQ (summary->loudspeakers[5].elevation, -90.0f);
    EXPECT_EQ (summary->subChannel, 1);
    EXPECT_FLOAT_EQ (summary->lowPassHz, 150.0f);
    EXPECT_FLOAT_EQ (summary->highPassHz, 150.0f);
}

TEST (DecoderSummary, NoSubAndAFileWithoutLayoutOrDate)
{
    auto file = juce::JSON::parse (shipped ("main.decoder.json"));
    file.getDynamicObject()->removeProperty ("LoudspeakerLayout");
    file.getDynamicObject()->setProperty ("Description", "hand made");
    auto parameters = juce::JSON::parse (shipped ("main.parameters.json")).clone();
    parameters.getDynamicObject()->setProperty ("swMode", 0.0);
    parameters.getDynamicObject()->setProperty ("weights", 1.0);

    juce::String error;
    const auto summary = a3::decoderSummary (juce::JSON::toString (file), parameters, error);
    ASSERT_TRUE (summary.has_value()) << error;
    EXPECT_TRUE (summary->loudspeakers.empty());
    EXPECT_TRUE (summary->exportedOn.isEmpty());
    EXPECT_TRUE (summary->madeWith.isEmpty());
    EXPECT_EQ (summary->subChannel, 0);
    EXPECT_EQ (summary->weights, "maxrE");
}

TEST (DecoderSummary, NotADecoderIsRefused)
{
    juce::String error;
    EXPECT_FALSE (a3::decoderSummary ("{", {}, error).has_value());
    EXPECT_FALSE (error.isEmpty());
    EXPECT_FALSE (a3::decoderSummary (R"({"Name":"x"})", {}, error).has_value());
    EXPECT_TRUE (error.contains ("Matrix")) << error;
}
