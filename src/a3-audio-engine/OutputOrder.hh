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

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstddef>
#include <vector>

namespace a3
{

/** Which device output each box of a layout is on.
 *
 *  The identity until the speaker test says otherwise: 5.1 alone is ordered
 *  one way by Windows, another by macOS and a third by some receivers, and a
 *  wrong order sounds like a broken app. Changed only by swapping two boxes,
 *  so it is a permutation at all times. */
class OutputOrder
{
public:
  explicit OutputOrder (std::size_t numChannels);

  std::size_t numChannels () const;
  std::size_t deviceChannelFor (std::size_t layoutChannel) const;

  void swap (std::size_t layoutChannelA, std::size_t layoutChannelB);

  /** Writes `layout` into `device` through the order. Outputs no box is on
   *  are cleared; boxes with no output on this device are dropped. Does not
   *  allocate, so it may run on the audio thread. */
  void apply (juce::AudioBuffer<float> const &layout,
              juce::AudioBuffer<float> &device) const;

private:
  std::vector<std::size_t> _toDevice;
};

}
