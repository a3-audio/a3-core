// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Engine.h"
#include "EngineConfig.h"
#include "SpatialConfig.h"
#include <string>
#include <vector>

namespace a3app
{
struct PortNames { std::vector<std::string> inputs, outputs; };

// The engine's ports as the backend registers them. Outputs are in exactly the
// order of Engine::outputMeters(): main (sub?, tops), booth (sub?, tops), rec L/R,
// phones L/R, then per channel pre L/R, post L/R. Inputs: analog_1..A, then per
// deck slot (letter A, B, ... in names, R12): deck{X}_1..16, cue L/R, aux L/R.
PortNames enginePortNames (const a3::EngineConfig&, const a3::SpatialConfig&);

constexpr int deckInputChannels = 16;
std::string deckLetters (int slotOneBased);   // 1 = "A", 26 = "Z", 27 = "AA"

// Whether a name of enginePortNames is one of the engine's inputs (analog_*, deck*_*),
// which tells a link's direction: a peer feeds an input, an output feeds a peer.
bool isEngineInputPort (const std::string& name);

// Where each port's buffer goes, in exactly enginePortNames' order. bindPortSlots sizes
// `in` and `out` for the config once (so the audio thread only writes pointers into them)
// and returns pointers to their slots; they stay valid while `in` and `out` are not resized.
struct PortSlots { std::vector<const float**> inputs; std::vector<float**> outputs; };
PortSlots bindPortSlots (const a3::EngineConfig&, const a3::SpatialConfig&, a3::EngineInputs& in, a3::EngineOutputs& out);
}
