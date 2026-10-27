/*

  A3 Motion UI
  Copyright (C) 2023 Patric Schmitz

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <gtest/gtest.h>

#include <a3-audio-engine/SpeakerTest.hh>

#include <cmath>

using namespace a3;

namespace
{
constexpr int sampleRate = 48000;
constexpr int longStep = 10 * sampleRate;  // never steps within a test

float
rmsDbfs (juce::AudioBuffer<float> const &buffer, int channel, int start, int length)
{
  return juce::Decibels::gainToDecibels (buffer.getRMSLevel (channel, start, length),
                                         -200.f);
}
}

TEST (SpeakerTest, OnlyTheActiveBoxSounds)
{
  SpeakerTest test (4, longStep);
  juce::AudioBuffer<float> buffer (4, 512);

  test.render (buffer);

  EXPECT_GT (buffer.getRMSLevel (0, 0, 512), 0.f);
  for (int channel = 1; channel < 4; ++channel)
    EXPECT_FLOAT_EQ (buffer.getMagnitude (channel, 0, 512), 0.f);
}

// The noise plays at the level the class says, whatever that is set to.
// Measured over the second second: the filter needs a moment to settle.
TEST (SpeakerTest, TheNoisePlaysAtItsStatedLevel)
{
  SpeakerTest test (1, longStep);
  juce::AudioBuffer<float> buffer (1, 2 * sampleRate);

  test.render (buffer);

  EXPECT_NEAR (rmsDbfs (buffer, 0, sampleRate, sampleRate), SpeakerTest::levelDbfs, 1.f);
}

TEST (SpeakerTest, AfterOneStepTheNextBoxSounds)
{
  SpeakerTest test (4, 100);
  juce::AudioBuffer<float> buffer (4, 150);

  test.render (buffer);

  EXPECT_EQ (test.activeChannel (), 1u);
  EXPECT_FLOAT_EQ (buffer.getMagnitude (0, 100, 50), 0.f);
  EXPECT_FLOAT_EQ (buffer.getMagnitude (1, 0, 100), 0.f);
  EXPECT_GT (buffer.getRMSLevel (1, 100, 50), 0.f);
}

TEST (SpeakerTest, AfterTheLastBoxItStartsAgain)
{
  SpeakerTest test (3, 10);
  juce::AudioBuffer<float> buffer (3, 30);

  test.render (buffer);

  EXPECT_EQ (test.activeChannel (), 0u);
}

// A layout with more boxes than the device has outputs: while the test is on
// a box the device cannot play, it is silent rather than writing past the end.
TEST (SpeakerTest, ABoxTheDeviceDoesNotHaveIsSilent)
{
  SpeakerTest test (6, 10);
  juce::AudioBuffer<float> buffer (2, 40);  // boxes 0..3 across the block

  test.render (buffer);

  EXPECT_FLOAT_EQ (buffer.getMagnitude (0, 20, 20), 0.f);
  EXPECT_FLOAT_EQ (buffer.getMagnitude (1, 20, 20), 0.f);
}

TEST (SpeakerTest, TheSameSeedGivesTheSameNoise)
{
  SpeakerTest first (1, longStep, 7);
  SpeakerTest second (1, longStep, 7);
  juce::AudioBuffer<float> a (1, 256);
  juce::AudioBuffer<float> b (1, 256);

  first.render (a);
  second.render (b);

  for (int sample = 0; sample < 256; ++sample)
    EXPECT_FLOAT_EQ (a.getSample (0, sample), b.getSample (0, sample));
}
