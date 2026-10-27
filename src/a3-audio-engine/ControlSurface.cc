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

#include <a3-audio-engine/ControlSurface.hh>

#include <cstring>

namespace a3
{

static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
               "the audio thread must never wait for a direction");

ControlSurface::ControlSurface (std::size_t numChannels)
    : _numChannels (numChannels < maxChannels ? numChannels : maxChannels)
{
}

std::size_t
ControlSurface::numChannels () const
{
  return _numChannels;
}

void
ControlSurface::setDirection (std::size_t channel, SourceDirection direction)
{
  if (channel >= _numChannels)
    return;
  _directions[channel].store (pack (direction), std::memory_order_release);
}

SourceDirection
ControlSurface::direction (std::size_t channel) const
{
  if (channel >= _numChannels)
    return {};
  return unpack (_directions[channel].load (std::memory_order_acquire));
}

std::uint64_t
ControlSurface::pack (SourceDirection direction)
{
  std::uint32_t azimuth = 0;
  std::uint32_t elevation = 0;
  std::memcpy (&azimuth, &direction.azimuthDegrees, sizeof azimuth);
  std::memcpy (&elevation, &direction.elevationDegrees, sizeof elevation);
  return (std::uint64_t{ azimuth } << 32) | elevation;
}

SourceDirection
ControlSurface::unpack (std::uint64_t bits)
{
  auto const azimuth = static_cast<std::uint32_t> (bits >> 32);
  auto const elevation = static_cast<std::uint32_t> (bits & 0xffffffffu);
  SourceDirection direction;
  std::memcpy (&direction.azimuthDegrees, &azimuth, sizeof azimuth);
  std::memcpy (&direction.elevationDegrees, &elevation, sizeof elevation);
  return direction;
}

}
