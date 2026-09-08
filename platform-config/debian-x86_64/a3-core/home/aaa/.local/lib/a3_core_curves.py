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


def load_curves(recorded):
    """The golden file, as curves that can be inverted."""
    return {name: Curve(name, points) for name, points in recorded.items()}


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
