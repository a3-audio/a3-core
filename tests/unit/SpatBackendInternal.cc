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

#include <a3-audio-engine/SpatBackendInternal.hh>
#include <a3-motion-engine/MotionEngine.hh>
#include <a3-motion-engine/elevation/HeightMapSphere.hh>

#include <memory>

using namespace a3;

TEST (SpatBackendInternal, APositionArrivesAsTheAnglesItWasSentWith)
{
  ControlSurface surface (4);
  SpatBackendInternal backend (surface);

  backend.sendPosition (2, Pos::fromSpherical (-60.f, 30.f, 1.f));

  EXPECT_NEAR (surface.direction (2).azimuthDegrees, -60.f, 1e-3f);
  EXPECT_NEAR (surface.direction (2).elevationDegrees, 30.f, 1e-3f);
}

TEST (SpatBackendInternal, OnlyTheAddressedChannelMoves)
{
  ControlSurface surface (4);
  SpatBackendInternal backend (surface);

  backend.sendPosition (1, Pos::fromSpherical (90.f, 0.f, 1.f));

  EXPECT_FLOAT_EQ (surface.direction (0).azimuthDegrees, 0.f);
  EXPECT_NEAR (surface.direction (1).azimuthDegrees, 90.f, 1e-3f);
}

// freq, Q and 3d reach the Isolator band and the crossfade, and neither exists
// before the app's second delivery. They are dropped on purpose; this pins
// that dropping them leaves the position alone.
TEST (SpatBackendInternal, TheThreePotsChangeNothingYet)
{
  ControlSurface surface (4);
  SpatBackendInternal backend (surface);

  backend.sendPot1 (0, 0.7f);
  backend.sendPot2 (0, 0.3f);
  backend.sendPot3 (0, 1.f);

  EXPECT_FLOAT_EQ (surface.direction (0).azimuthDegrees, 0.f);
  EXPECT_FLOAT_EQ (surface.direction (0).elevationDegrees, 0.f);
}

TEST (SpatBackendInternal, TheEngineCanSendThroughIt)
{
  ControlSurface surface (4);
  HeightMapSphere heightMap;
  MotionEngine engine (4, heightMap,
                       std::make_unique<SpatBackendInternal> (surface));
  SUCCEED ();
}
