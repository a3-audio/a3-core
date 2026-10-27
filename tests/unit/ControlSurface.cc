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

#include <a3-audio-engine/ControlSurface.hh>

#include <atomic>
#include <thread>

using namespace a3;

TEST (ControlSurface, EveryChannelStartsStraightAhead)
{
  ControlSurface const surface (4);
  for (std::size_t channel = 0; channel < 4; ++channel)
    {
      EXPECT_FLOAT_EQ (surface.direction (channel).azimuthDegrees, 0.f);
      EXPECT_FLOAT_EQ (surface.direction (channel).elevationDegrees, 0.f);
    }
}

TEST (ControlSurface, ADirectionComesBackWithBothAngles)
{
  ControlSurface surface (4);
  surface.setDirection (1, { -135.5f, 42.25f });
  EXPECT_FLOAT_EQ (surface.direction (1).azimuthDegrees, -135.5f);
  EXPECT_FLOAT_EQ (surface.direction (1).elevationDegrees, 42.25f);
}

TEST (ControlSurface, ChannelsDoNotShareADirection)
{
  ControlSurface surface (4);
  surface.setDirection (0, { 90.f, 10.f });
  surface.setDirection (3, { -90.f, -10.f });
  EXPECT_FLOAT_EQ (surface.direction (0).azimuthDegrees, 90.f);
  EXPECT_FLOAT_EQ (surface.direction (3).azimuthDegrees, -90.f);
  EXPECT_FLOAT_EQ (surface.direction (1).azimuthDegrees, 0.f);
}

// A channel number from a bigger device, or from a set written on one, must
// not reach past the end -- it is dropped and reads back as straight ahead.
TEST (ControlSurface, AChannelItDoesNotHaveIsIgnored)
{
  ControlSurface surface (4);
  surface.setDirection (4, { 45.f, 45.f });
  EXPECT_FLOAT_EQ (surface.direction (4).azimuthDegrees, 0.f);
  EXPECT_FLOAT_EQ (surface.direction (4).elevationDegrees, 0.f);
}

TEST (ControlSurface, MoreChannelsThanItCanHoldAreCapped)
{
  ControlSurface const surface (ControlSurface::maxChannels + 5);
  EXPECT_EQ (surface.numChannels (), ControlSurface::maxChannels);
}

// The reason the two angles travel as one: with two atomics the audio thread
// can read a new azimuth next to the old elevation, and for one block the
// sound sits somewhere nobody sent it. The writer only ever stores pairs whose
// two halves are equal, so any unequal pair read back is a torn one.
TEST (ControlSurface, TheAudioThreadNeverSeesHalfADirection)
{
  ControlSurface surface (1);
  std::atomic<bool> done{ false };

  std::thread writer ([&] {
    for (int i = 0; i < 200000; ++i)
      surface.setDirection (0, i % 2 ? SourceDirection{ 10.f, 10.f }
                                     : SourceDirection{ -20.f, -20.f });
    done = true;
  });

  int torn = 0;
  while (!done)
    {
      auto const direction = surface.direction (0);
      if (direction.azimuthDegrees != direction.elevationDegrees)
        ++torn;
    }
  writer.join ();

  EXPECT_EQ (torn, 0);
}
