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


def send_levels(cues, mix, unity):
    """{"decks": [{"pre": .., "post": ..}, ...], "stem": .., "return": {..}}
    for the decks' cue flags and the knob (0 cue .. 1 mix).

    The return is on the mix side like a deck's post-fader send, and on the
    cue side while any deck is cued: a cued deck brings its FX along (spec
    stemdeck-remote). The stems' cue send carries StemDeck's CUE bus, always
    on the cue side: StemDeck decides what is on that bus (stem CUE switches,
    the decks' PHONES buttons) and it is silent otherwise."""
    x = min(1.0, max(0.0, float(mix)))
    cue_side = unity * math.cos(x * math.pi / 2)
    mix_side = unity * math.sin(x * math.pi / 2)
    return {"decks": [{"pre": cue_side if on else 0.0, "post": mix_side} for on in cues],
            "stem": cue_side,
            "return": {"pre": cue_side if any(cues) else 0.0, "post": mix_side}}
