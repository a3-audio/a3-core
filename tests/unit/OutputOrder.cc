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

#include <a3-audio-engine/OutputOrder.hh>

using namespace a3;

namespace
{
juce::AudioBuffer<float>
bufferWithChannelNumbers (int numChannels, int numSamples)
{
  juce::AudioBuffer<float> buffer (numChannels, numSamples);
  for (int channel = 0; channel < numChannels; ++channel)
    for (int sample = 0; sample < numSamples; ++sample)
      buffer.setSample (channel, sample, static_cast<float> (channel + 1));
  return buffer;
}
}

TEST (OutputOrder, StartsAsEachBoxOnItsOwnNumber)
{
  OutputOrder const order (6);
  for (std::size_t channel = 0; channel < 6; ++channel)
    EXPECT_EQ (order.deviceChannelFor (channel), channel);
}

// What the speaker test's "that was the wrong box" does. A swap can only ever
// exchange two outputs, so the order stays a permutation whatever is pressed:
// no box can end up on two outputs, and none on no output.
TEST (OutputOrder, ASwapExchangesTwoOutputs)
{
  OutputOrder order (6);
  order.swap (2, 3);
  EXPECT_EQ (order.deviceChannelFor (2), 3u);
  EXPECT_EQ (order.deviceChannelFor (3), 2u);
  EXPECT_EQ (order.deviceChannelFor (4), 4u);
}

TEST (OutputOrder, ASwapWithAChannelItDoesNotHaveChangesNothing)
{
  OutputOrder order (4);
  order.swap (1, 9);
  EXPECT_EQ (order.deviceChannelFor (1), 1u);
}

TEST (OutputOrder, ApplyRoutesEachBoxToItsOutput)
{
  OutputOrder order (3);
  order.swap (0, 2);
  auto const layout = bufferWithChannelNumbers (3, 8);
  juce::AudioBuffer<float> device (3, 8);

  order.apply (layout, device);

  EXPECT_FLOAT_EQ (device.getSample (2, 5), 1.f);  // box 0 now on output 2
  EXPECT_FLOAT_EQ (device.getSample (0, 5), 3.f);  // box 2 now on output 0
  EXPECT_FLOAT_EQ (device.getSample (1, 5), 2.f);
}

// A device with more outputs than the layout has boxes -- a 12-output bus
// playing quad. The outputs no box is on must be silent, not left holding
// whatever was in the buffer before.
TEST (OutputOrder, OutputsNoBoxIsOnAreSilent)
{
  OutputOrder const order (2);
  auto const layout = bufferWithChannelNumbers (2, 8);
  auto device = bufferWithChannelNumbers (4, 8);

  order.apply (layout, device);

  EXPECT_FLOAT_EQ (device.getMagnitude (2, 0, 8), 0.f);
  EXPECT_FLOAT_EQ (device.getMagnitude (3, 0, 8), 0.f);
}

// A layout with more boxes than the device has outputs -- 7.1.4 into a stereo
// interface. The boxes that have nowhere to go are dropped, not written past
// the end.
TEST (OutputOrder, BoxesWithoutAnOutputAreDropped)
{
  OutputOrder const order (6);
  auto const layout = bufferWithChannelNumbers (6, 8);
  juce::AudioBuffer<float> device (2, 8);

  order.apply (layout, device);

  EXPECT_FLOAT_EQ (device.getSample (0, 0), 1.f);
  EXPECT_FLOAT_EQ (device.getSample (1, 0), 2.f);
}
