/*

  A3 Core -- parked audio engine
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

// AN EXTRACT, NOT A COMPILE UNIT.
//
// The host half of Plan 1 (2026-09-21), as it stood in a3-motion-ui at
// ff56cf3 behind `#ifdef A3_AUDIO_ENGINE_ENABLED`: the parts of
// src/a3-motion-ui/StandaloneApp.{hh,cc} and
// src/a3-motion-ui/A3MotionAudioProcessor.{hh,cc} that opened an audio device
// and rendered into it. They were removed there on 2026-10-06 (Motion UI is a
// pure OSC interface) and are kept here, verbatim, as the starting point for
// whoever hosts the engine in Core. The surrounding classes are not repeated;
// each block names the class it belonged to.
//
// What the rig taught about this code (2026-09-22, see engine/README.md):
// JUCE's JACK device connects its ports on open and registers one port per
// channel of the target client; Plan 2 decided to register twelve named JACK
// ports itself instead (no auto-connect).

// ---------------------------------------------------------------------------
// src/a3-motion-ui/CMakeLists.txt (target "a3-motion-ui")
// ---------------------------------------------------------------------------
//
//   set(A3_AUDIO_ENGINE_ENABLED FALSE CACHE BOOL "Run the audio engine inside the app")
//
//   target_compile_definitions("a3-motion-ui" PUBLIC
//       $<$<BOOL:${A3_AUDIO_ENGINE_ENABLED}>:A3_AUDIO_ENGINE_ENABLED>
//       # JACK only with the engine: jackd holds the sound card on the dev machine,
//       # so ALSA alone cannot reach it. JUCE dlopens libjack at runtime and needs
//       # only its headers to build; the device build opens no audio at all.
//       $<$<BOOL:${A3_AUDIO_ENGINE_ENABLED}>:JUCE_JACK=1>
//   )
//
//   target_link_libraries("a3-motion-ui" PUBLIC
//       a3-audio-engine
//       juce::juce_audio_utils
//       juce::juce_audio_devices)

// ---------------------------------------------------------------------------
// A3MotionAudioProcessor (juce::AudioProcessor)
// ---------------------------------------------------------------------------

#include <a3-audio-engine/ChunkedRender.hh>
#include <a3-audio-engine/OutputOrder.hh>
#include <a3-audio-engine/SpeakerTest.hh>

#include <cstdlib>

// --- class members -----------------------------------------------------------
//
//   // Public so the host in StandaloneApp asks the device for exactly these.
//   static constexpr int numInputs = 4;
//   static constexpr int numOutputs = 12;  // up to 7.1.4
//
// private:
//   void renderAudio (juce::AudioBuffer<float> &buffer);
//
//   juce::AudioBuffer<float> _layoutBuffer;
//   OutputOrder _outputOrder{ numOutputs };
//   // Only while A3_SPEAKER_TEST is set: until the output list exists (plan 2)
//   // this is the one way to start it.
//   std::unique_ptr<SpeakerTest> _speakerTest;

A3MotionAudioProcessor::BusesProperties
A3MotionAudioProcessor::busesForThisBuild ()
{
  // Four channels in, as the desk's USB hands them over; twelve out, the most
  // any listed layout needs.
  return BusesProperties ()
      .withInput ("Input", juce::AudioChannelSet::discreteChannels (numInputs))
      .withOutput ("Output",
                   juce::AudioChannelSet::discreteChannels (numOutputs));
}

bool
A3MotionAudioProcessor::isMidiEffect () const
{
  // juce::AudioProcessorPlayer gives a MIDI effect no audio channels at all
  // (findMostSuitableLayout returns 0 in / 0 out), so processBlock would get
  // an empty buffer while the output bus still claims twelve channels.
  return false;
}

void
A3MotionAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
  // The only allocation: renderAudio() never resizes _layoutBuffer, so its
  // capacity has to be settled here, before the audio thread starts calling
  // processBlock.
  _layoutBuffer.setSize (numOutputs, samplesPerBlock);
  if (std::getenv ("A3_SPEAKER_TEST") != nullptr)
    {
      auto const twoSecondsPerBox = static_cast<int> (sampleRate * 2.0);
      _speakerTest = std::make_unique<SpeakerTest> (numOutputs, twoSecondsPerBox);
    }
}

void
A3MotionAudioProcessor::releaseResources ()
{
  // prepareToPlay makes a fresh one from A3_SPEAKER_TEST; a device restart
  // must not carry the old one's position across.
  _speakerTest.reset ();
}

void
A3MotionAudioProcessor::processBlock (juce::AudioBuffer<float> &buffer,
                                      juce::MidiBuffer &midiMessages)
{
  juce::ignoreUnused (midiMessages);
  renderAudio (buffer);
}

void
A3MotionAudioProcessor::renderAudio (juce::AudioBuffer<float> &buffer)
{
  auto output = getBusBuffer (buffer, false, 0);
  renderThroughOutputOrder (_speakerTest.get (), _outputOrder, _layoutBuffer,
                            output);
}

// ---------------------------------------------------------------------------
// StandaloneApp (juce::JUCEApplication)
// ---------------------------------------------------------------------------

// --- class members -----------------------------------------------------------
//
//   void startAudio ();
//   void openAudioDevice ();
//   void stopAudio ();
//
//   // Declared in this order so that, should shutdown() be skipped, the
//   // implicit destruction still runs in reverse: the device manager closes the
//   // device first (no more callbacks into the player), then the player lets go
//   // of the processor (calling its releaseResources), then the processor goes.
//   std::unique_ptr<juce::AudioProcessor> _processor;
//   juce::AudioProcessorPlayer _player;
//   juce::AudioDeviceManager _deviceManager;
//
// --- at the end of initialise(), after the window --------------------------
//
//   // After the window, not before: the config parse above and the UI's own
//   // construction can throw, and neither should leave an open audio device
//   // behind. The processor does not depend on the window, so nothing needs
//   // it earlier.
//   startAudio ();
//
// --- first thing in shutdown() ---------------------------------------------
//
//   stopAudio ();

namespace
{
juce::String
environmentValue (char const *name)
{
  auto const *value = std::getenv (name);
  return value != nullptr ? juce::String (value) : juce::String ();
}
}

void
StandaloneApp::startAudio ()
{
  // The processor exists only to render audio. The editor is never created
  // from it: the performer sees our own MainWindow, which is built above and
  // knows nothing of this processor.
  _processor.reset (createPluginFilter ());
  _player.setProcessor (_processor.get ());
  openAudioDevice ();
  _deviceManager.addAudioCallback (&_player);
}

void
StandaloneApp::openAudioDevice ()
{
  // Chosen by environment until plan 2 brings a selector in the UI.
  auto const requestedType = environmentValue ("A3_AUDIO_DEVICE_TYPE");
  auto const requestedOutput = environmentValue ("A3_AUDIO_OUTPUT_DEVICE");

  if (requestedType.isNotEmpty ())
    {
      // setCurrentAudioDeviceType only knows the types a scan has found.
      _deviceManager.getAvailableDeviceTypes ();
      _deviceManager.setCurrentAudioDeviceType (requestedType, true);
    }

  juce::AudioDeviceManager::AudioDeviceSetup setup;
  setup.outputDeviceName = requestedOutput;

  auto const error = _deviceManager.initialise (
      A3MotionAudioProcessor::numInputs, A3MotionAudioProcessor::numOutputs,
      nullptr, true, {}, requestedOutput.isNotEmpty () ? &setup : nullptr);

  // initialise() moves on to another type when the requested one has no
  // devices (JACK without a running server falls back to ALSA). Noise on an
  // output nobody asked for is worse than none, so that is refused.
  auto const openedType = _deviceManager.getCurrentAudioDeviceType ();
  if (requestedType.isNotEmpty () && openedType != requestedType)
    {
      juce::StringArray available;
      for (auto *type : _deviceManager.getAvailableDeviceTypes ())
        available.add (type->getTypeName ());

      juce::Logger::writeToLog ("audio: device type \"" + requestedType
                                + "\" not available (have: "
                                + available.joinIntoString (", ")
                                + "), no audio device opened");
      _deviceManager.closeAudioDevice ();
      return;
    }

  if (error.isNotEmpty ())
    juce::Logger::writeToLog ("audio: opening the device failed: " + error);

  auto *device = _deviceManager.getCurrentAudioDevice ();
  if (device == nullptr)
    {
      juce::Logger::writeToLog ("audio: no audio device open");
      return;
    }

  juce::Logger::writeToLog (
      "audio: opened " + openedType + " device \"" + device->getName ()
      + "\", " + juce::String (device->getActiveInputChannels ().countNumberOfSetBits ())
      + " in / "
      + juce::String (device->getActiveOutputChannels ().countNumberOfSetBits ())
      + " out, " + juce::String (device->getCurrentSampleRate ()) + " Hz, "
      + juce::String (device->getCurrentBufferSizeSamples ()) + " samples");
}

void
StandaloneApp::stopAudio ()
{
  // Strictly the reverse of startAudio().
  _deviceManager.removeAudioCallback (&_player);
  _player.setProcessor (nullptr);
  _deviceManager.closeAudioDevice ();
  _processor = nullptr;
}
