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

#include <a3-audio-engine/SpeakerTest.hh>

namespace a3
{

namespace
{
// Paul Kellet's economy pink filter gives 1.7168 RMS for uniform white noise
// in [-1, 1] (20 s simulated, 2026-09-21). Derived from levelDbfs rather than
// written as a second number, so turning the level down cannot leave the
// gain behind.
constexpr float pinkFilterRms = 1.7168f;
float const pinkGain
    = juce::Decibels::decibelsToGain (SpeakerTest::levelDbfs) / pinkFilterRms;
}

SpeakerTest::SpeakerTest (std::size_t numChannels, int samplesPerStep, juce::int64 seed)
    : _numChannels (numChannels > 0 ? numChannels : 1),
      _samplesPerStep (samplesPerStep > 0 ? samplesPerStep : 1),
      _random (seed)
{
}

std::size_t
SpeakerTest::activeChannel () const
{
  return _active;
}

void
SpeakerTest::render (juce::AudioBuffer<float> &buffer)
{
  buffer.clear ();

  for (int sample = 0; sample < buffer.getNumSamples (); ++sample)
    {
      auto const value = nextPinkSample () * pinkGain;
      if (static_cast<int> (_active) < buffer.getNumChannels ())
        buffer.setSample (static_cast<int> (_active), sample, value);

      if (++_samplesIntoStep >= _samplesPerStep)
        {
          _samplesIntoStep = 0;
          _active = (_active + 1) % _numChannels;
        }
    }
}

float
SpeakerTest::nextPinkSample ()
{
  auto const white = _random.nextFloat () * 2.f - 1.f;
  _b0 = 0.99765f * _b0 + white * 0.0990460f;
  _b1 = 0.96300f * _b1 + white * 0.2965164f;
  _b2 = 0.57000f * _b2 + white * 1.0526913f;
  return _b0 + _b1 + _b2 + white * 0.1848f;
}

}
