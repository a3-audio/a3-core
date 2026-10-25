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

// 0..1 -> 0..1 through Core's ten-point constant-power table (aux send control).
double slopeConstantPower (double control);

// REAPER send scale, linear in dB from -40 dB to 0 dB, as an amplitude 0..1.
// 0 is silence (ruling R2), not REAPER's -40 dB floor.
double sendGain (double sendValue);

// Aux send: sendGain (slopeConstantPower (control)).
double auxSendGain (double control);

// Cue and mix sides of the phones knob (0 cue .. 1 mix), a3_core_cue on REAPER's send scale.
double cueWeight (double phonesMix);
double mixWeight (double phonesMix);

// DJ filter cutoff in Hz. Core's slope_fx_freq_* gives the normalised v
// (HP 0.2 + 0.6x, LP 0.8 - 0.6x); TAL's own map is not readable (Step 0 G),
// so v runs log 20 Hz..20 kHz: 20 * 1000^v.
double filterCutoffHz (bool highPass, double control);

// Core's slope_fx_res (identity) -> 0..1; the filter maps it to its own Q.
double filterResonance (double control);

// Silence at the bottom of a gain control (ruling R18, decision D23): a control
// below this is exactly 0, not PurestGain's -40 dB floor. Applies to VOLUME,
// TRIM/GAIN, the moving share of 3D, and the MASTER/BOOTH/RETURN/PHONES stages.
bool isSilent (double control);

// Linear gain of a PurestGain-law stage (slopeVolume through A*80-40 dB), 0 when
// isSilent (control). The MASTER, BOOTH, RETURN and PHONES stages use this.
double stageGain (double control);

constexpr double purestGainUnity = 0.5;   // A for 0 dB
}
