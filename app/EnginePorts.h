// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
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
}
