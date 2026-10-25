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

TEST (Laws, SlopeConstantPowerMatchesGolden) { expectMatches ("slope_constant_power", a3::laws::slopeConstantPower); }

// Golden rows x -> normalised v; the HP law is v = 0.2 + 0.6x, the LP law v = 0.8 - 0.6x.
TEST (Laws, FilterNormalisedMatchesGoldenHipassLopass)
{
    const auto hp = golden()["slope_fx_freq_hipass"];
    const auto lp = golden()["slope_fx_freq_lopass"];
    ASSERT_TRUE (hp.isArray());
    ASSERT_TRUE (lp.isArray());
    for (const auto& p : *hp.getArray())
        EXPECT_NEAR (a3::laws::filterCutoffHz (true, (double) p[0]), 20.0 * std::pow (1000.0, (double) p[1]), 1e-6) << (double) p[0];
    for (const auto& p : *lp.getArray())
        EXPECT_NEAR (a3::laws::filterCutoffHz (false, (double) p[0]), 20.0 * std::pow (1000.0, (double) p[1]), 1e-6) << (double) p[0];
}

TEST (Laws, FilterResonanceMatchesGolden) { expectMatches ("slope_fx_res", a3::laws::filterResonance); }

TEST (Laws, SendZeroIsSilence)
{
    EXPECT_EQ (a3::laws::sendGain (0.0), 0.0);
    EXPECT_DOUBLE_EQ (a3::laws::sendGain (1.0), 1.0);
    EXPECT_NEAR (20.0 * std::log10 (a3::laws::sendGain (0.5)), -20.0, 1e-9);
}

TEST (Laws, AuxSendGainIsSendOfConstantPower)
{
    for (double x : { 0.0, 0.05, 0.3, 0.5, 0.9, 1.0 })
        EXPECT_DOUBLE_EQ (a3::laws::auxSendGain (x), a3::laws::sendGain (a3::laws::slopeConstantPower (x))) << x;
}

// Source: a3-core .../.local/lib/a3_core_cue.py send_levels([True], mix, 1.0, False)["decks"][0],
// {mix, pre (cue side), post (mix side)}. Unity 1.0 is REAPER's send scale.
TEST (Laws, CueAndMixMatchCoreSendLevels)
{
    struct Row { double mix, pre, post; };
    const Row rows[] {
        { 0.0,  1.0,                    0.0 },
        { 0.25, 0.9238795325112867,     0.3826834323650898 },
        { 0.5,  0.7071067811865476,     0.7071067811865475 },
        { 0.75, 0.38268343236508984,    0.9238795325112867 },
        { 1.0,  6.123233995736766e-17,  1.0 },
    };
    for (const auto& r : rows)
    {
        EXPECT_NEAR (a3::laws::cueWeight (r.mix), a3::laws::sendGain (r.pre), 1e-9) << r.mix;
        EXPECT_NEAR (a3::laws::mixWeight (r.mix), a3::laws::sendGain (r.post), 1e-9) << r.mix;
    }
}

TEST (Laws, FilterCutoffEndPoints)
{
    const double low = 20.0 * std::pow (1000.0, 0.2);
    const double high = 20.0 * std::pow (1000.0, 0.8);
    EXPECT_NEAR (a3::laws::filterCutoffHz (true, 0.0), low, 1e-9);
    EXPECT_NEAR (a3::laws::filterCutoffHz (false, 1.0), low, 1e-9);
    EXPECT_NEAR (a3::laws::filterCutoffHz (true, 1.0), high, 1e-9);
    EXPECT_NEAR (a3::laws::filterCutoffHz (false, 0.0), high, 1e-9);

    double previousHp = 0.0, previousLp = 1e9;
    for (int i = 0; i <= 100; ++i)
    {
        const double hp = a3::laws::filterCutoffHz (true, i / 100.0);
        const double lp = a3::laws::filterCutoffHz (false, i / 100.0);
        EXPECT_GT (hp, previousHp);
        EXPECT_LT (lp, previousLp);
        previousHp = hp;
        previousLp = lp;
    }
}

TEST (Laws, NaNIsZero)
{
    const double nan = std::nan ("");
    EXPECT_EQ (a3::laws::slopeConstantPower (nan), 0.0);
    EXPECT_EQ (a3::laws::sendGain (nan), 0.0);
    EXPECT_EQ (a3::laws::auxSendGain (nan), 0.0);
    EXPECT_EQ (a3::laws::filterResonance (nan), 0.0);
    EXPECT_DOUBLE_EQ (a3::laws::cueWeight (nan), a3::laws::cueWeight (0.0));
    EXPECT_DOUBLE_EQ (a3::laws::mixWeight (nan), a3::laws::mixWeight (0.0));
    EXPECT_DOUBLE_EQ (a3::laws::filterCutoffHz (true, nan), a3::laws::filterCutoffHz (true, 0.0));
    EXPECT_DOUBLE_EQ (a3::laws::filterCutoffHz (false, nan), a3::laws::filterCutoffHz (false, 0.0));
}
