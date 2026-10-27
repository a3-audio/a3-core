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

namespace a3
{

class OutputOrder;
class SpeakerTest;

/** Renders one host block into `device`, through `order`.
 *
 *  `scratch` is the layout buffer prepared before the audio thread started;
 *  its getNumSamples() is the chunk capacity. The host may hand over more
 *  samples than it announced in prepareToPlay (JUCE documents that figure as
 *  a planning hint, not a hard limit), and nothing here may allocate, so
 *  `scratch` is never resized: the block is walked in chunks of at most that
 *  capacity through non-owning views over both buffers. Each chunk gets the
 *  speaker test (or silence, when `speakerTest` is null) and then the order.
 *
 *  A capacity of 0 -- prepareToPlay has not run -- clears `device`. */
void renderThroughOutputOrder (SpeakerTest *speakerTest, OutputOrder const &order,
                               juce::AudioBuffer<float> &scratch,
                               juce::AudioBuffer<float> &device);

}
