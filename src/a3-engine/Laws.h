// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace a3::laws
{
// Each law maps a control in 0..1 to a plugin parameter. Inputs are clamped
// to 0..1 and NaN maps to 0, because the values arrive over UDP unchecked.

// 0..1 -> PurestGain A, 0..0.5 (Core: np.interp [0,1] -> [0,0.5]).
double slopeVolume (double control);

// 0..1 -> SmoothEQ3 0..0.6 through Core's ten-point table.
double slopeEq (double control);

// 3D 0..1 -> PurestGain A for the moving band (Core: a3_core_crossfade._band_gain).
double bandGain (double threeD);

// 0..1 -> 0.05..0.9, for Isolator3 frequency and Q (Core: np.interp).
double isolatorParam (double control);

constexpr double purestGainUnity = 0.5;   // A for 0 dB
}
