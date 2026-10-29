// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include "DelayModulation.h"

namespace
{
constexpr double twoPi = 6.283185307179586;

std::unique_ptr<a3::IemNode> makeDualDelay()
{
    auto node = a3::IemNode::create (a3::IemKind::dualDelay, 16, 16);
    EXPECT_TRUE (node != nullptr);
    return node;
}

float pitchL (a3::IemNode& node) { return node.rawParameter ("pitchL")->load(); }
}

TEST (DelayModulation, SetsBasePlusDepthTimesShape)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "pitchL", 10.0f, 18.0f, 0.25f, "sine" } });

    modulation.tick (0.0, *node);       // 0.5 + 0.5 sin 0 = 0.5
    EXPECT_NEAR (pitchL (*node), 10.0f + 18.0f * 0.5f, 0.06f);   // the parameter snaps to 0.1

    modulation.tick (1.0, *node);       // quarter period: sine at its peak
    EXPECT_NEAR (pitchL (*node), 10.0f + 18.0f, 0.06f);

    modulation.tick (3.0, *node);       // three quarters: sine at its trough
    EXPECT_NEAR (pitchL (*node), 10.0f, 0.06f);
}

TEST (DelayModulation, TriangleRunsZeroToOneToZero)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "pitchL", 0.0f, 18.0f, 0.5f, "triangle" } });
    modulation.tick (0.0, *node);
    EXPECT_NEAR (pitchL (*node), 0.0f, 0.06f);
    modulation.tick (1.0, *node);       // half a period
    EXPECT_NEAR (pitchL (*node), 18.0f, 0.06f);
    modulation.tick (0.5, *node);
    EXPECT_NEAR (pitchL (*node), 9.0f, 0.06f);
}

TEST (DelayModulation, RateZeroKeepsTheBase)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "pitchL", 7.0f, 18.0f, 0.0f, "random" } });
    for (double t : { 0.0, 1.0, 123.4 })
    {
        modulation.tick (t, *node);
        EXPECT_NEAR (pitchL (*node), 7.0f, 0.06f);
    }
}

TEST (DelayModulation, RandomStaysInTheRangeAndIsDeterministic)
{
    auto first = makeDualDelay();
    auto second = makeDualDelay();
    const std::vector<a3::ModulatedParameter> spec { { "pitchL", 0.0f, 18.0f, 0.125f, "random" } };
    a3::DelayModulation a (spec), b (spec);
    float lowest = 1000.0f, highest = -1000.0f;
    for (int step = 0; step < 4000; ++step)    // 80 s at 50 Hz: ten targets
    {
        a.tick (step * 0.02, *first);
        b.tick (step * 0.02, *second);
        EXPECT_EQ (pitchL (*first), pitchL (*second));
        lowest = std::min (lowest, pitchL (*first));
        highest = std::max (highest, pitchL (*first));
    }
    EXPECT_GE (lowest, 0.0f);
    EXPECT_LE (highest, 18.0f);
    EXPECT_GT (highest - lowest, 3.0f);        // it moves
}

TEST (DelayModulation, RandomInterpolatesLinearlyBetweenTargets)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "pitchL", 0.0f, 18.0f, 0.1f, "random" } });   // a target every 10 s
    modulation.tick (0.0, *node);
    const float start = pitchL (*node);
    modulation.tick (10.0, *node);
    const float end = pitchL (*node);
    modulation.tick (5.0, *node);      // time may be queried at any point inside a segment
    EXPECT_NEAR (pitchL (*node), 0.5f * (start + end), 0.12f);
}

TEST (DelayModulation, DrivesEachParameterWithItsOwnRate)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "pitchL", 0.0f, 18.0f, 0.25f, "sine" },
                                      { "pitchR", 0.0f, 9.0f, 0.0f, "sine" } });
    modulation.tick (1.0, *node);
    EXPECT_NEAR (node->rawParameter ("pitchL")->load(), 18.0f, 0.06f);
    EXPECT_NEAR (node->rawParameter ("pitchR")->load(), 0.0f, 0.06f);   // rate 0: base
}

TEST (DelayModulation, AnUnknownParameterIsIgnoredNotACrash)
{
    auto node = makeDualDelay();
    a3::DelayModulation modulation ({ { "noSuchId", 0.0f, 1.0f, 1.0f, "sine" } });
    modulation.tick (0.3, *node);
    SUCCEED();
}

TEST (DelayModulation, NeverRunsOnTheAudioThread)
{
    // tick() takes the node and a time, never a buffer: there is nothing an audio callback could hand it.
    static_assert (! std::is_invocable_v<decltype (&a3::DelayModulation::tick), a3::DelayModulation&,
                                         juce::AudioBuffer<float>&>);
    SUCCEED();
}

namespace
{
float raw (a3::IemNode& node, const char* id) { return node.rawParameter (id)->load(); }

// Both lines at a known feedback (-6 / -15 dB, f + x = 0.679), cutoffs 233 / 8000 Hz and yaw +-60.
std::unique_ptr<a3::IemNode> knownDualDelay()
{
    auto node = makeDualDelay();
    for (const auto& [id, value] : std::initializer_list<std::pair<const char*, float>> {
             { "feedbackL", -6.0f }, { "feedbackR", -6.0f }, { "xfeedbackL", -15.0f }, { "xfeedbackR", -15.0f },
             { "HPcutOffL", 233.0f }, { "HPcutOffR", 233.0f }, { "LPcutOffL", 8000.0f }, { "LPcutOffR", 8000.0f },
             { "yawL", 60.0f }, { "yawR", -60.0f } })
        EXPECT_TRUE (node->setParameter (id, value).wasOk()) << id;
    return node;
}

constexpr double loopRate = 48000.0;
constexpr int loopBlock = 128;
}

TEST (DelayLoop, WritesTheTimeOfBothLinesFromTempoAndBeats)
{
    auto node = knownDualDelay();
    a3::DelayLoop loop (*node);
    loop.prepare (loopRate);
    const auto expect = [&] (float bpm, float mult)
    {
        for (const char* id : { "delayBPML", "delayBPMR" })
            EXPECT_FLOAT_EQ (raw (*node, id), bpm) << id;
        for (const char* id : { "delayMultL", "delayMultR" })
            EXPECT_FLOAT_EQ (raw (*node, id), mult) << id;
    };
    loop.set (120.0f, 1.0f, 0.7f, loopBlock);
    expect (120.0f, 1.0f);
    loop.set (120.0f, 0.25f, 0.7f, loopBlock);
    expect (240.0f, 2.0f);
    loop.set (80.0f, 2.0f, 0.7f, loopBlock);    // the time delayTimeFor gives, whatever R2's fallback
    const auto two = a3::delay::delayTimeFor (80.0f, 2.0f);
    expect (two.bpm, (float) two.mult);

    loop.set (std::nanf (""), 1.0f, 0.7f, loopBlock);
    loop.set (INFINITY, 1.0f, 0.7f, loopBlock);
    loop.set (120.0f, std::nanf (""), 0.7f, loopBlock);
    expect (two.bpm, (float) two.mult);         // non-finite: the time stays

    loop.set (5.0f, 1.0f, 0.7f, loopBlock);     // below 20: 20 BPM, a beat is 3 s, a quarter 0.75 s fits
    expect (80.0f, 1.0f);
    loop.set (1000.0f, 1.0f, 0.7f, loopBlock);  // above 400: 400 = 200 x 2
    expect (200.0f, 2.0f);
}

namespace
{
float shareOf (a3::IemNode& node)
{
    return juce::Decibels::decibelsToGain (raw (node, "feedbackL"), -60.0f)
           + juce::Decibels::decibelsToGain (raw (node, "xfeedbackL"), -60.0f);
}
}

// Review minor 1: each change is a linear ramp of the loop gain over a fixed 30 ms, so a block's
// step is that ramp's share of the change, however large the change.
TEST (DelayLoop, FeedbackRampsLinearlyOver30Ms)
{
    auto node = knownDualDelay();
    a3::DelayLoop loop (*node);
    loop.prepare (loopRate);
    const float ramp = 0.030f * (float) loopRate, blockShare = loopBlock / ramp;
    loop.set (120.0f, 0.25f, 0.0f, loopBlock);
    EXPECT_NEAR (shareOf (*node), 0.679f * (1.0f - blockShare), 1e-3f);

    const int blocksIn30Ms = (int) std::ceil (ramp / loopBlock);
    for (int b = 1; b < blocksIn30Ms; ++b)
        loop.set (120.0f, 0.25f, 0.0f, loopBlock);
    for (const char* id : { "feedbackL", "feedbackR", "xfeedbackL", "xfeedbackR" })
        EXPECT_FLOAT_EQ (raw (*node, id), -60.0f) << id;

    for (int b = 0; b < 3; ++b)
        loop.set (120.0f, 0.25f, 0.95f, loopBlock);
    const float third = 0.95f * 3.0f * blockShare;
    EXPECT_NEAR (shareOf (*node), third, 1e-3f);
    loop.set (120.0f, 0.25f, 0.5f, loopBlock);   // a change mid-ramp: a new 30 ms ramp from where it is
    EXPECT_NEAR (shareOf (*node), third + (0.5f - third) * blockShare, 1e-3f);
    for (int b = 0; b < blocksIn30Ms; ++b)
        loop.set (120.0f, 0.25f, 0.95f, loopBlock);
    EXPECT_NEAR (raw (*node, "feedbackL"), 20.0f * std::log10 (0.95f * 0.738f), 0.01f);
    EXPECT_NEAR (raw (*node, "feedbackR"), 20.0f * std::log10 (0.95f * 0.738f), 0.01f);
    EXPECT_NEAR (raw (*node, "xfeedbackL"), 20.0f * std::log10 (0.95f * 0.262f), 0.01f);
    EXPECT_NEAR (raw (*node, "xfeedbackR"), 20.0f * std::log10 (0.95f * 0.262f), 0.01f);

    loop.set (120.0f, 0.25f, std::nanf (""), loopBlock);
    EXPECT_NEAR (raw (*node, "feedbackL"), 20.0f * std::log10 (0.95f * 0.738f), 0.01f) << "non-finite: stays";
}

// R15 and review minor 6: whatever the saved fx says, the shape starts in rotate mode with pitch
// and roll 0, and both lines carry the left line's cutoffs and its yaw mirrored.
TEST (DelayShape, StartsInRotateModeWithBothLinesWritten)
{
    auto node = makeDualDelay();
    for (const auto& [id, value] : std::initializer_list<std::pair<const char*, float>> {
             { "transformModeL", 1.0f }, { "transformModeR", 1.0f }, { "pitchL", 13.8f }, { "pitchR", 2.1f },
             { "rollL", 5.0f }, { "rollR", -5.0f }, { "yawL", 10.0f }, { "yawR", 119.6f },
             { "HPcutOffL", 300.0f }, { "HPcutOffR", 100.0f }, { "LPcutOffL", 6000.0f }, { "LPcutOffR", 9000.0f } })
        ASSERT_TRUE (node->setParameter (id, value).wasOk()) << id;
    a3::DelayShape shape (*node);
    for (const auto& [id, value] : std::initializer_list<std::pair<const char*, float>> {
             { "transformModeL", 0.0f }, { "transformModeR", 0.0f }, { "pitchL", 0.0f }, { "pitchR", 0.0f },
             { "rollL", 0.0f }, { "rollR", 0.0f }, { "yawL", 10.0f }, { "yawR", -10.0f },
             { "HPcutOffL", 300.0f }, { "HPcutOffR", 300.0f }, { "LPcutOffL", 6000.0f }, { "LPcutOffR", 6000.0f } })
        EXPECT_NEAR (raw (*node, id), value, 0.06f) << id;
}

TEST (DelayOwnership, TheEngineOwnsTheDelaysModeTurnFiltersFeedbackAndTime)
{
    for (const char* stem : { "transformMode", "yaw", "pitch", "roll", "HPcutOff", "LPcutOff", "feedback", "xfeedback",
                              "delayBPM", "delayMult" })
        for (const char* side : { "L", "R" })
            EXPECT_TRUE (a3::ownsDelayParameter (juce::String (stem) + side)) << stem << side;
    for (const char* free : { "lfoRateL", "lfoDepthR", "wetGainL", "dryGain", "warpFactorAzL", "orderSetting" })
        EXPECT_FALSE (a3::ownsDelayParameter (free)) << free;

    const auto kept = a3::withoutOwnedParameters ({ { "pitchL", 0.0f, 18.0f, 0.1f, "random" },
                                                    { "lfoDepthL", 0.0f, 0.5f, 0.2f, "sine" },
                                                    { "yawR", 0.0f, 9.0f, 0.1f, "sine" } });
    ASSERT_EQ (kept.size(), 1u);
    EXPECT_EQ (kept[0].parameter, "lfoDepthL");
}

TEST (DelayOwnership, TheNoteNamesWhatASavedFileLosesAndNothingElse)
{
    const auto fx = [] (const char* json) { return juce::JSON::parse (json); };
    EXPECT_TRUE (a3::delayOverrideNote (fx (R"({"transformModeL": 0, "transformModeR": 0, "pitchL": 0, "pitchR": 0,
                                                 "yawL": 60, "yawR": -60, "HPcutOffL": 233})"), {}).isEmpty())
        << "the shipped data: the engine sets what it says";
    EXPECT_TRUE (a3::delayOverrideNote (fx ("{}"), { { "lfoDepthL", 0.0f, 0.5f, 0.2f, "sine" } }).isEmpty());

    const auto reaper = a3::delayOverrideNote (fx (R"({"transformModeL": 1, "transformModeR": 1, "pitchL": 13.8,
                                                        "pitchR": 0, "rollR": 4, "yawL": 10})"),
                                               { { "pitchL", 0.0f, 18.0f, 0.1f, "random" } });
    for (const char* named : { "transformModeL 1", "transformModeR 1", "pitchL 13.8", "rollR 4", "delayModulation", "pitchL" })
        EXPECT_TRUE (reaper.contains (named)) << named << ": " << reaper;
    EXPECT_FALSE (reaper.contains ("pitchR")) << reaper;
    EXPECT_FALSE (reaper.contains ("yawL")) << "the delay page sets the yaw anyway: " << reaper;
    EXPECT_FALSE (reaper.containsChar ('\n'));
}

TEST (DelayShape, SpreadTurnsTheLinesApartTenDegreesATick)
{
    auto node = knownDualDelay();
    a3::DelayShape shape (*node);
    shape.tick (0.0f, 180.0f, *node);
    EXPECT_NEAR (raw (*node, "yawL"), 70.0f, 0.06f);
    EXPECT_NEAR (raw (*node, "yawR"), -70.0f, 0.06f);
    for (int t = 0; t < 11; ++t)
        shape.tick (0.0f, 180.0f, *node);
    EXPECT_NEAR (raw (*node, "yawL"), 180.0f, 0.06f);
    EXPECT_NEAR (raw (*node, "yawR"), -180.0f, 0.06f);
    for (int t = 0; t < 18; ++t)
        shape.tick (0.0f, 0.0f, *node);
    EXPECT_NEAR (raw (*node, "yawL"), 0.0f, 0.06f);
    EXPECT_NEAR (raw (*node, "yawR"), 0.0f, 0.06f);
}

TEST (DelayShape, ColorGlidesBothLinesFiltersAThirdOfAnOctaveATick)
{
    auto node = knownDualDelay();
    a3::DelayShape shape (*node);
    shape.tick (-1.0f, 60.0f, *node);
    for (const char* id : { "HPcutOffL", "HPcutOffR" })
        EXPECT_NEAR (raw (*node, id), 233.0f * std::cbrt (2.0f), 1.0f) << id;
    for (int t = 0; t < 10; ++t)
        shape.tick (-1.0f, 60.0f, *node);
    for (const char* id : { "HPcutOffL", "HPcutOffR" })
        EXPECT_NEAR (raw (*node, id), 2500.0f, 1.0f) << id;
    for (const char* id : { "LPcutOffL", "LPcutOffR" })
        EXPECT_NEAR (raw (*node, id), 8000.0f, 1.0f) << id;

    for (int t = 0; t < 30; ++t)
        shape.tick (1.0f, 60.0f, *node);
    for (const char* id : { "HPcutOffL", "HPcutOffR" })
        EXPECT_NEAR (raw (*node, id), 233.0f, 1.0f) << id;
    for (const char* id : { "LPcutOffL", "LPcutOffR" })
        EXPECT_NEAR (raw (*node, id), 500.0f, 1.0f) << id;
    EXPECT_NEAR (raw (*node, "yawL"), 60.0f, 0.06f) << "a colour leaves the spread";
}

TEST (DelayShape, NeverRunsOnTheAudioThread)
{
    static_assert (! std::is_invocable_v<decltype (&a3::DelayShape::tick), a3::DelayShape&, juce::AudioBuffer<float>&>);
    SUCCEED();
}
