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

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace a3
{

/** A direction in the degrees Motion computes (Position::azimuth/elevation)
 *  and the IEM MultiEncoder takes. */
struct SourceDirection
{
  float azimuthDegrees = 0.f;
  float elevationDegrees = 0.f;
};

/** Where each channel's sound is, handed from the thread that decides it to
 *  the audio thread that renders it.
 *
 *  Azimuth and elevation travel packed into one 64-bit atomic rather than as
 *  two: two atomics let the audio thread read a new azimuth beside the old
 *  elevation, and for one block the sound sits somewhere nobody sent it. */
class ControlSurface
{
public:
  static constexpr std::size_t maxChannels = 8;

  explicit ControlSurface (std::size_t numChannels);

  std::size_t numChannels () const;

  void setDirection (std::size_t channel, SourceDirection direction);
  SourceDirection direction (std::size_t channel) const;

private:
  static std::uint64_t pack (SourceDirection direction);
  static SourceDirection unpack (std::uint64_t bits);

  std::size_t const _numChannels;
  // Zero bits are 0.f, 0.f: every channel starts straight ahead.
  std::array<std::atomic<std::uint64_t>, maxChannels> _directions{};
};

}
