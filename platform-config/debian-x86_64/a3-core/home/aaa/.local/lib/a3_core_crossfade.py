"""How much of a channel moves: the 3D law, as two REAPER gains.

One A3 value in, two PurestGain values out. A channel is split in REAPER:
`n-multi-enc` is the steady bed over every speaker; `n-stereo-enc` carries
two Isolator3 instances behind one gain container -- the isolated band, which
the MultiEncoder moves, and the same band cut from a phase-inverted copy of
the steady signal, which subtracts it from the bed. See apply_3d_crossfade()
in a3-core.py; the name `stereo` there is historical, and so is `crossfade`
here (it was one until 2026-10-06).

It arrives on `/channel/n/3d`, A3 Motion's per-channel pot.

It lives here, and not inline in a3-core.py, because the caller does a second
thing besides sending: it writes the value into Core's own memory, so a recall
can answer with it. Forgetting that does not fail -- Core simply answers the
next recall with a value from before somebody turned the pot, and the room
hears the sound snap back. That is the kind of omission a separate, tested
function is for.

No numpy and no sockets, so it is importable and testable anywhere. Same door
as a3_core_layout and a3_core_buttons.
"""

import math

#: PurestGain's law: dB = A * 80 - 40. A 0.5 is 0 dB, A 0 the -40 dB floor.
PUREST_GAIN_UNITY = 0.5
PUREST_GAIN_FLOOR_DB = -40.0
PUREST_GAIN_RANGE_DB = 80.0


def crossfade_gains(value):
    """The (stereo, multi) PurestGain values for a 3D position.

    **The steady bed never changes.** Multi stays at 0 dB for every 3D value.
    3D only sets how much of the isolated band moves: the stereo-enc gain is
    3D read as an amplitude, 0..1, in PurestGain's law -- 1 is 0 dB, 0.5 is
    -6 dB, 0.1 is -20 dB, 0.01 and below the -40 dB floor (A 0).

    That one gain scales both the moving band and the phase-inverted band
    taken out of the bed, so band + remainder = input at every position:
    turning 3D moves sound, it does not make the channel louder or quieter.
    The crossfade this replaced (decided 2026-10-06, finding F14) lowered the
    bed above 0.5 while the subtraction stayed, and the channel was not
    level-neutral.

    A gain cannot say where the control was below -40 dB, and REAPER never
    reports it back as a 3D value -- which is why Core keeps the value instead,
    see a3_core_recall.REMEMBERED_CONTROLS.

    Clamped, because UDP carries whatever it is handed and a gain above 0.5
    would be louder than the input.
    """
    x = min(1.0, max(0.0, float(value)))
    return _band_gain(x), PUREST_GAIN_UNITY


def _band_gain(amplitude):
    if amplitude <= 0.0:
        return 0.0
    db = 20 * math.log10(amplitude)
    a = (db - PUREST_GAIN_FLOOR_DB) / PUREST_GAIN_RANGE_DB
    return min(PUREST_GAIN_UNITY, max(0.0, a))


#: The two Isolator3 instances on n-stereo-enc, by layout fx-slot name: the
#: band that moves, and the same band cut from the phase-inverted steady copy.
BAND_FILTER_SLOTS = ("enc_pots", "enc_pots_inverted")


def band_filter_messages(layout, track, pot, value):
    """The REAPER messages for one filter pot (frequency or Q): both Isolators,
    the same parameter, the same value.

    The subtraction only cancels the band when both filter alike. Isolator #2
    was meant to follow #1 through a REAPER parameter link, which measurably
    did not (F14, 2026-10-06), so Core writes both itself.
    """
    param = layout.fx_param(pot)
    return [(layout.address("fx_param", track=track,
                            slot=layout.fx_slot(slot), param=param), value)
            for slot in BAND_FILTER_SLOTS]
