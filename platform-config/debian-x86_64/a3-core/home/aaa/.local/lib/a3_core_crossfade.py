"""Where a channel sits between its stereo and its multi encoder.

One A3 value in, two REAPER gains out. It arrives on two addresses and means
the same thing on both: `/channel/n/fx-send`, which is the A3 Mixer's pot and
how this was reached before Motion existed, and `/channel/n/3d`, which is A3
Motion's per-channel pot. Both roads have to stay open until the mixer's knob
is given back its own job -- see
issues/a3-core-fx-send-fuehrt-noch-die-3d-funktion.md.

It lives here, and not twice in a3-core.py, because both roads now have to do
a second thing besides sending: write the value into Core's own memory, so a
recall can answer with it. Two copies of one decision is two places for the
second one to be forgotten, and forgetting it here does not fail -- it just
means Core answers a recall with a value from before somebody turned the pot,
and the room hears the sound snap back.

No numpy and no sockets, so it is importable and testable anywhere. Same door
as a3_core_layout and a3_core_buttons.
"""


def crossfade_gains(value):
    """The (stereo, multi) gains for a control position.

    Both ends are open at the middle and one at each extreme: the control
    fades between the two encoders rather than between the encoder and
    silence, so 0.5 is the loudest either gets.

    **Which way round it goes is the one mistake this can make.** An earlier
    gain branch had it reversed -- turning the control up faded the stereo
    encoder *out* -- and it reads as a plausible sign flip either way round.
    Stereo rises with the control; multi falls. tests/test_core_crossfade.py
    holds that rather than a comment asking nicely.

    A single gain cannot say where the control was: below the middle the multi
    gain is flat at 0.5, above it the stereo gain is. That is why there is no
    way back from REAPER for this, and why Core keeps the value instead --
    see a3_core_recall.REMEMBERED_CONTROLS.

    Clamped, because UDP carries whatever it is handed and a gain above 0.5
    would be louder than the control can ask for.
    """
    x = min(1.0, max(0.0, float(value)))
    stereo_gain = 0.5 * min(1.0, x * 2)
    multi_gain = 0.5 * (1 - max(0.0, (x - 0.5) * 2))
    return stereo_gain, multi_gain
