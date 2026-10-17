// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stands in for the JuceHeader.h that Projucer/juce_generate_juce_header would
// write next to each IEM plug-in. IEM includes it both as <JuceHeader.h> and as
// "../JuceLibraryCode/JuceHeader.h", so the IEM sources compile unchanged.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>   // BinauralDecoder reads its IRs as WAV
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>   // juce::Vector3D, used by IEM's geometry
#include <juce_osc/juce_osc.h>

#include "BinaryData.h"

// An IEM plug-in is an audio effect: no MIDI, not a synth.
#ifndef JucePlugin_WantsMidiInput
    #define JucePlugin_WantsMidiInput 0
#endif
#ifndef JucePlugin_ProducesMidiOutput
    #define JucePlugin_ProducesMidiOutput 0
#endif
#ifndef JucePlugin_IsMidiEffect
    #define JucePlugin_IsMidiEffect 0
#endif
#ifndef JucePlugin_IsSynth
    #define JucePlugin_IsSynth 0
#endif
