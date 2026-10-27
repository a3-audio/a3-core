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

namespace a3
{

/** Pink noise, one box at a time -- what every AV receiver does, and the one
 *  way a wrong output order shows itself before it spoils a set.
 *
 *  Steps on by itself after `samplesPerStep`, counted in rendered samples, so
 *  it needs no timer and runs on the audio thread. */
class SpeakerTest
{
public:
  /** Quiet on purpose: this plays straight to the interface, past REAPER and
   *  its master, into whatever the amps are set to. -20 dBFS, the AV
   *  receiver's level, was asked to be "relativ leise" before the first run
   *  on the rig. */
  static constexpr float levelDbfs = -40.f;

  SpeakerTest (std::size_t numChannels, int samplesPerStep, juce::int64 seed = 1);

  std::size_t activeChannel () const;

  /** Clears `buffer` and writes the noise into the active box's channel. */
  void render (juce::AudioBuffer<float> &buffer);

private:
  float nextPinkSample ();

  std::size_t const _numChannels;
  int const _samplesPerStep;
  int _samplesIntoStep = 0;
  std::size_t _active = 0;
  juce::Random _random;
  float _b0 = 0.f;
  float _b1 = 0.f;
  float _b2 = 0.f;
};

}
