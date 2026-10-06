"""The headphones' crossfade, in the channel buses' sends.

Since the template of 2026-10-01 there are no PFL tracks and no ph-mix: each
channel bus sends to enc_phones twice -- pre-fader (the cue) and post-fader
(the mix) -- and the stems track sends StemDeck's phones to dec_phones as a
cue of its own. The phones-mix knob fades cue (left) into mix (right) at constant
power; a cue send only opens while its cue is on, the mix sends follow the
knob alone.

The levels are on REAPER's send scale, where the template's 0 dB reads
`unity` (1.0, 2026-10-01). Below that REAPER's scale is not measured yet, so
cos/sin here are on that scale and the result is checked by ear on the rig.
Pure: no OSC, no REAPER.
"""

import math


def send_levels(cues, mix, unity, return_cue):
    """{"decks": [{"pre": .., "post": ..}, ...], "stem": .., "return": {..}}
    for the decks' cue flags, the knob (0 cue .. 1 mix) and the return's own
    cue flag.

    A deck's cue send (pre-fader) opens while its cue is on, whether the
    channel plays its analog input or a stem: the cue carries what the
    channel makes of it, filter and EQ (2026-10-06; StemDeck's C used to cue
    the raw stem).

    The return is on the mix side like a deck's post-fader send. On the cue
    side only with its own cue (spec return-cue, 2026-10-04): it carries
    stems or the analog return, not a deck's FX, so a deck's cue does not
    bring it along. The stems' cue send carries StemDeck's CUE bus, always
    on the cue side: StemDeck decides what is on that bus (stem CUE switches,
    the decks' PHONES buttons) and it is silent otherwise."""
    x = min(1.0, max(0.0, float(mix)))
    cue_side = unity * math.cos(x * math.pi / 2)
    mix_side = unity * math.sin(x * math.pi / 2)
    return {"decks": [{"pre": cue_side if on else 0.0, "post": mix_side}
                      for on in cues],
            "stem": cue_side,
            "return": {"pre": cue_side if return_cue else 0.0, "post": mix_side}}
