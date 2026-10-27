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

#include <a3-audio-engine/ChunkedRender.hh>

#include <a3-audio-engine/OutputOrder.hh>
#include <a3-audio-engine/SpeakerTest.hh>

#include <algorithm>

namespace a3
{

void
renderThroughOutputOrder (SpeakerTest *speakerTest, OutputOrder const &order,
                          juce::AudioBuffer<float> &scratch,
                          juce::AudioBuffer<float> &device)
{
  auto const numSamples = device.getNumSamples ();
  auto const chunkCapacity = scratch.getNumSamples ();

  if (chunkCapacity == 0)
    {
      device.clear ();
      return;
    }

  for (auto start = 0; start < numSamples; start += chunkCapacity)
    {
      auto const chunk = std::min (chunkCapacity, numSamples - start);

      juce::AudioBuffer<float> layoutView (scratch.getArrayOfWritePointers (),
                                            scratch.getNumChannels (), 0, chunk);
      juce::AudioBuffer<float> deviceView (device.getArrayOfWritePointers (),
                                            device.getNumChannels (), start, chunk);

      if (speakerTest != nullptr)
        speakerTest->render (layoutView);
      else
        layoutView.clear ();

      order.apply (layoutView, deviceView);
    }
}

}
