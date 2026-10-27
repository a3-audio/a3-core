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

#include <a3-audio-engine/OutputOrder.hh>

#include <numeric>
#include <utility>

namespace a3
{

OutputOrder::OutputOrder (std::size_t numChannels) : _toDevice (numChannels)
{
  std::iota (_toDevice.begin (), _toDevice.end (), std::size_t{ 0 });
}

std::size_t
OutputOrder::numChannels () const
{
  return _toDevice.size ();
}

std::size_t
OutputOrder::deviceChannelFor (std::size_t layoutChannel) const
{
  return _toDevice.at (layoutChannel);
}

void
OutputOrder::swap (std::size_t layoutChannelA, std::size_t layoutChannelB)
{
  if (layoutChannelA >= _toDevice.size () || layoutChannelB >= _toDevice.size ())
    return;
  std::swap (_toDevice[layoutChannelA], _toDevice[layoutChannelB]);
}

void
OutputOrder::apply (juce::AudioBuffer<float> const &layout,
                    juce::AudioBuffer<float> &device) const
{
  device.clear ();

  auto const numSamples = juce::jmin (layout.getNumSamples (), device.getNumSamples ());
  auto const numBoxes = juce::jmin (static_cast<std::size_t> (layout.getNumChannels ()),
                                    _toDevice.size ());

  for (std::size_t box = 0; box < numBoxes; ++box)
    {
      auto const output = static_cast<int> (_toDevice[box]);
      if (output >= device.getNumChannels ())
        continue;
      device.copyFrom (output, 0, layout, static_cast<int> (box), 0, numSamples);
    }
}

}
