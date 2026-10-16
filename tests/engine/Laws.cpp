// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <juce_core/juce_core.h>
#include "Laws.h"

namespace
{
juce::var golden()
{
    return juce::JSON::parse (juce::File (A3_CURVES_GOLDEN));
}
// The golden file holds [[x, y], ...] per law, recorded from Core.
void expectMatches (const char* name, double (*law) (double))
{
    const auto points = golden()[name];
    ASSERT_TRUE (points.isArray()) << name;
    for (const auto& p : *points.getArray())
        EXPECT_NEAR (law ((double) p[0]), (double) p[1], 1e-9) << name << " at " << (double) p[0];
}
}

TEST (Laws, VolumeMatchesCore)  { expectMatches ("slope_volume", a3::laws::slopeVolume); }
TEST (Laws, EqMatchesCore)      { expectMatches ("slope_eq", a3::laws::slopeEq); }

TEST (Laws, BandGainMatchesCoresCrossfade)
{
    const auto points = golden()["crossfade_gains"];   // [x, [stereo, multi]]
    for (const auto& p : *points.getArray())
        EXPECT_NEAR (a3::laws::bandGain ((double) p[0]), (double) p[1][0], 1e-6) << (double) p[0];
}

TEST (Laws, IsolatorParamSpansCoresRange)
{
    EXPECT_DOUBLE_EQ (a3::laws::isolatorParam (0.0), 0.05);
    EXPECT_DOUBLE_EQ (a3::laws::isolatorParam (1.0), 0.9);
    EXPECT_NEAR (a3::laws::isolatorParam (0.5), 0.475, 1e-12);
}

// Review focus 2: UDP carries whatever it is handed.
TEST (Laws, GarbageIsClampedNeverNan)
{
    for (double bad : { -1.0, 7.0, std::nan (""), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() })
    {
        EXPECT_FALSE (std::isnan (a3::laws::slopeVolume (bad)));
        EXPECT_FALSE (std::isnan (a3::laws::slopeEq (bad)));
        EXPECT_FALSE (std::isnan (a3::laws::bandGain (bad)));
        EXPECT_FALSE (std::isnan (a3::laws::isolatorParam (bad)));
    }
    EXPECT_DOUBLE_EQ (a3::laws::slopeVolume (7.0), 0.5);
    EXPECT_DOUBLE_EQ (a3::laws::slopeVolume (-1.0), 0.0);
}
