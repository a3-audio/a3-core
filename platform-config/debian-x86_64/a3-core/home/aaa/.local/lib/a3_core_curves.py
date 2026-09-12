"""Turning a REAPER value back into the A3 value that produced it.

Every value Core sends is bent by a curve on the way out -- seven of them, and
exactly one value goes out unbent. So REAPER's feedback comes back in REAPER's
units, and relaying it raw would put Motion's knob in the wrong place.

The inverse is possible at all because the curves are pinned:
tools/curve-characterisation/curves-golden.json holds all eleven at every
hundredth, recorded from the Python that runs. That characterisation was made
so the curves could be *ported* with certainty; this is a second use for it,
and a closer one.

No numpy and no sockets, so it is importable and testable anywhere. Same door
as a3_core_layout.
"""

from bisect import bisect_left


class CurveNotInvertible(Exception):
    """This curve cannot answer "which input gave that output".

    One curve returns two numbers from one input (the stereo/multi crossfade
    gains). A single number cannot say which input it came from, and answering
    anyway would be the confident kind of wrong -- so it refuses.
    """


class Curve:
    """One curve's recorded points, ready to be read backwards."""

    def __init__(self, name, points):
        self.name = name
        self.inputs = [p[0] for p in points]
        outputs = [p[1] for p in points]

        # A curve whose output is itself a list -- the crossfade returns a
        # stereo gain and a multi gain together.
        self.single_valued = not any(isinstance(o, (list, tuple))
                                     for o in outputs)
        self.outputs = outputs if self.single_valued else []

        if self.single_valued:
            self.rising = self.outputs[-1] >= self.outputs[0]


#: The mappings that are not curves: a straight line from an A3 range to a
#: REAPER range, as (a3 low, a3 high, reaper low, reaper high).
#:
#: The encoder pots are sent with np.interp(v, [0, 1], [0.05, 0.9]) and never
#: touched a curve. Recording that as an eleventh golden curve would be
#: inventing a measurement -- curves-golden.json holds what the running Python
#: *did*, and nothing recorded this because there was nothing to record. It is
#: written down as the arithmetic it is.
#:
#: These four numbers have to agree with a3-core.py's pot_1/pot_2 branches.
#: Nothing checks that automatically: the coverage test walks calls named
#: slope_*, and an np.interp send is not one.
#:
#: **Nothing inverts them at the moment.** This was built on 2026-09-12 for a
#: reverse path that relayed the pots back to Motion continuously, and that
#: turned out to be a feedback loop -- REAPER holds the base value with the
#: accent envelope on top, Motion holds the base, and writing the one into
#: the other ratchets. The entries came out the same day; see
#: issues/a3-core-dauernder-rueckweg-ist-eine-schleife.md.
#:
#: Kept rather than deleted because the arithmetic is right and tested, and
#: because answering a recall *on request* needs exactly it. If that is never
#: built, this should go rather than sit here looking used.
LINEAR_MAPS = {
    "linear_enc_pot": (0.0, 1.0, 0.05, 0.9),
}


class LinearMap:
    """A straight line between two ranges, read backwards.

    Same job as a Curve and nothing in common underneath: a recorded table is
    searched, this is arithmetic. It clamps at both ends for the same reason
    a Curve does -- np.interp clamps on the way out, so REAPER can be holding
    a value A3 cannot produce, and the end of the range is the honest answer.
    """

    def __init__(self, name, a3_low, a3_high, out_low, out_high):
        self.name = name
        self.a3_low, self.a3_high = a3_low, a3_high
        self.out_low, self.out_high = out_low, out_high
        self.single_valued = True

    def inverted(self, output, tell_me=False):
        span = self.out_high - self.out_low
        fraction = (output - self.out_low) / span if span else 0.0
        fraction = min(1.0, max(0.0, fraction))
        value = self.a3_low + fraction * (self.a3_high - self.a3_low)

        # Always exact: a straight line has no plateau, so there is never a
        # range of inputs behind one output. Said out loud rather than left
        # for a caller to assume.
        return (value, True) if tell_me else value


def load_curves(recorded):
    """The golden file, as curves that can be inverted -- plus the maps that
    were never curves.

    Both kinds arrive in one dict because a3-core.py looks everything up in
    one dict, by the name its reverse-table entry carries. A reverse entry
    naming something that is not here finds nothing and gives up quietly,
    which is the failure this avoids.
    """
    curves = {name: Curve(name, points) for name, points in recorded.items()}
    curves.update({name: LinearMap(name, *numbers)
                   for name, numbers in LINEAR_MAPS.items()})
    return curves


def invert(curve, output, tell_me=False):
    """The input that produced `output`.

    Interpolated between the recorded points, so a value landing between two
    hundredths is still answered.

    Outside the curve's range it clamps rather than extrapolating: REAPER can
    hold a value A3 cannot produce -- somebody moved a fader past where the
    curve reaches -- and the end of the range is the honest answer, where a
    number off the end of the table would be an invention.

    On a plateau, where one output came from a range of inputs, it answers
    with the lowest of them. `tell_me=True` returns (value, exact) so a caller
    can know it was not sure; three of the eleven curves have plateaus and
    somebody will meet one.
    """
    # Two kinds of thing can be inverted here and they share nothing below
    # this line: a recorded table is searched, a straight line is arithmetic.
    # Dispatched rather than given a common base class, because this one call
    # is the only thing they would share.
    if isinstance(curve, LinearMap):
        return curve.inverted(output, tell_me)

    if not curve.single_valued:
        raise CurveNotInvertible(
            f"{curve.name} returns more than one number per input")

    outs = curve.outputs if curve.rising else curve.outputs[::-1]
    ins = curve.inputs if curve.rising else curve.inputs[::-1]

    # At or past an end.
    #
    # A curve that saturates reaches its last output well before its last
    # input -- slope_eq holds 0.6 from 0.90 on -- so clamping to the end of
    # the table would answer 1.0 where the lowest input producing that value
    # is 0.90. The rule is the same at an end as in the middle: the lowest
    # input that produces this output, and "not sure" when there is more than
    # one.
    if output <= outs[0]:
        sure = output == outs[0] and outs[1] != outs[0]
        return (ins[0], sure) if tell_me else ins[0]

    if output >= outs[-1]:
        first = len(outs) - 1
        while first > 0 and outs[first - 1] == outs[-1]:
            first -= 1
        sure = output == outs[-1] and first == len(outs) - 1
        return (ins[first], sure) if tell_me else ins[first]

    index = bisect_left(outs, output)
    lower, upper = outs[index - 1], outs[index]

    if upper == lower:
        # A plateau: this output came from every input across it. The lowest
        # is as good an answer as any and is at least stable.
        return (ins[index - 1], False) if tell_me else ins[index - 1]

    part = (output - lower) / (upper - lower)
    value = ins[index - 1] + (ins[index] - ins[index - 1]) * part

    if not tell_me:
        return value

    # Sure unless the answer sits at the edge of a flat stretch: the same
    # output on either side of it would give a different input.
    flat = (index > 1 and outs[index - 2] == lower)
    return (value, not flat)
