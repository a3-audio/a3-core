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

#include <a3-audio-engine/ChunkedRender.hh>
#include <a3-audio-engine/OutputOrder.hh>
#include <a3-audio-engine/SpeakerTest.hh>

using namespace a3;

namespace
{
constexpr int capacity = 64;
constexpr int longStep = 480000;  // never steps within a test

juce::AudioBuffer<float>
filledWith (int numChannels, int numSamples, float value)
{
  juce::AudioBuffer<float> buffer (numChannels, numSamples);
  for (int channel = 0; channel < numChannels; ++channel)
    juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), value,
                                       numSamples);
  return buffer;
}
}

// Three whole chunks and a partial one: every device sample is written exactly
// once, in order. Compared against the same noise rendered in one piece, so a
// gap at a chunk seam, a chunk written twice or a lost tail all show.
TEST (ChunkedRender, ABlockLargerThanTheCapacityIsRenderedWhole)
{
  constexpr int numSamples = 3 * capacity + 20;
  SpeakerTest chunked (1, longStep, 3);
  SpeakerTest whole (1, longStep, 3);
  OutputOrder const order (1);
  juce::AudioBuffer<float> scratch (1, capacity);
  auto device = filledWith (1, numSamples, 5.f);
  juce::AudioBuffer<float> reference (1, numSamples);

  renderThroughOutputOrder (&chunked, order, scratch, device);
  whole.render (reference);

  for (int sample = 0; sample < numSamples; ++sample)
    ASSERT_FLOAT_EQ (device.getSample (0, sample), reference.getSample (0, sample))
        << "at sample " << sample;
}

// Before prepareToPlay the scratch buffer has no room; the device must still
// come out silent rather than keep whatever the host left in it.
TEST (ChunkedRender, NoCapacityClearsTheDevice)
{
  SpeakerTest test (2, longStep);
  OutputOrder const order (2);
  juce::AudioBuffer<float> scratch (2, 0);
  auto device = filledWith (2, 128, 1.f);

  renderThroughOutputOrder (&test, order, scratch, device);

  EXPECT_FLOAT_EQ (device.getMagnitude (0, 128), 0.f);
}

// The step counts rendered samples, not chunks: with a step of 100 and chunks
// of 64, box 0 owns device samples 0..99 and box 1 takes over at 100, in the
// middle of the second chunk.
TEST (ChunkedRender, AStepInsideAChunkLandsOnItsSample)
{
  SpeakerTest test (2, 100);
  OutputOrder const order (2);
  juce::AudioBuffer<float> scratch (2, capacity);
  auto device = filledWith (2, 200, 5.f);

  renderThroughOutputOrder (&test, order, scratch, device);

  EXPECT_NE (device.getSample (0, 99), 0.f);
  EXPECT_FLOAT_EQ (device.getMagnitude (0, 100, 100), 0.f);
  EXPECT_FLOAT_EQ (device.getMagnitude (1, 0, 100), 0.f);
  EXPECT_NE (device.getSample (1, 100), 0.f);
  EXPECT_GT (device.getRMSLevel (1, 100, 100), 0.f);
}

TEST (ChunkedRender, WithoutASpeakerTestTheDeviceIsSilent)
{
  OutputOrder const order (2);
  juce::AudioBuffer<float> scratch (2, capacity);
  auto device = filledWith (2, 3 * capacity + 20, 1.f);

  renderThroughOutputOrder (nullptr, order, scratch, device);

  EXPECT_FLOAT_EQ (device.getMagnitude (0, device.getNumSamples ()), 0.f);
}

// The order is applied on every chunk, not only the first.
TEST (ChunkedRender, TheOutputOrderIsAppliedToEveryChunk)
{
  constexpr int numSamples = 3 * capacity + 20;
  SpeakerTest test (2, longStep);
  OutputOrder order (2);
  order.swap (0, 1);
  juce::AudioBuffer<float> scratch (2, capacity);
  auto device = filledWith (2, numSamples, 5.f);

  renderThroughOutputOrder (&test, order, scratch, device);

  EXPECT_FLOAT_EQ (device.getMagnitude (0, 0, numSamples), 0.f);
  EXPECT_NE (device.getSample (1, numSamples - 1), 0.f);
  EXPECT_GT (device.getRMSLevel (1, 3 * capacity, 20), 0.f);
}
