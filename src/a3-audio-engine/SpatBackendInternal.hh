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

#pragma once

#include <a3-audio-engine/ControlSurface.hh>
#include <a3-motion-engine/backends/SpatBackend.hh>

namespace a3
{

/** Motion's positions, delivered inside the same process instead of over OSC.
 *
 *  freq, Q and 3d go to the Isolator band and the crossfade, and neither
 *  exists before the app's second delivery. They are dropped on purpose, not
 *  forgotten. */
class SpatBackendInternal : public SpatBackend
{
public:
  explicit SpatBackendInternal (ControlSurface &surface);

  void sendPosition (index_t channel, Pos const &pos) override;
  void sendPot1 (index_t channel, float pot1) override;
  void sendPot2 (index_t channel, float pot2) override;
  void sendPot3 (index_t channel, float pot3) override;

protected:
  // Nothing is addressed: there is no network between Motion and the engine.
  void addressesChanged (OscAddresses const &addresses) override;

private:
  ControlSurface &_surface;
};

}
