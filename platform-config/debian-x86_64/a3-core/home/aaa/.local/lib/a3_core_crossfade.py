"""How much of a channel moves: the balance between its two tracks.

One A3 value in, two REAPER gains out. Both go to the *same* MultiEncoder --
the moving track on its channels 1-4, the steady one on 5-n over every
speaker at once. See apply_3d_crossfade() in a3-core.py for what the two
tracks do to each other; the name `stereo` there is historical.

It arrives on `/channel/n/3d`, A3 Motion's per-channel pot.

It used to arrive on `/channel/n/fx-send` as well -- the mixer's pot was the
only continuous control the desk had for this before Motion existed. Since
2026-09-12 that pot is the FX send again and this has one road. The decision
and its price (the desk has no 3D control any more) are in
issues/a3-core-fx-send-fuehrt-noch-die-3d-funktion.md.

It lives here, and not inline in a3-core.py, because the caller does a second
thing besides sending: it writes the value into Core's own memory, so a recall
can answer with it. Forgetting that does not fail -- Core simply answers the
next recall with a value from before somebody turned the pot, and the room
hears the sound snap back. That is the kind of omission a separate, tested
function is for.

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
