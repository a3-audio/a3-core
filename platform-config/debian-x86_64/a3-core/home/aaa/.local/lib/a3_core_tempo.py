"""Das Tempo weitergeben, aber nicht jeden Wimpernschlag.

The beat-analyzer sends `/beat beat bar bpm` once per beat -- better than
twice a second at club tempo. What needs the tempo is the delay on the FX
bus, and it only needs to hear about it when it has actually changed.

**A delay is the one effect where chasing a tempo is audible.** Rewriting a
delay line's length shifts the pitch of whatever is still in it; a filter can
be followed smoothly, a delay time cannot. IEM's DualDelay 1.15 fixed the
worst of that ("fixing clicks and crackling if delay is changed rapidly"),
which is why this is a deadband and not a whole holding-and-settling
machine -- but a message per beat would still be a rewrite per beat for no
gain.

So: what came in, what was last passed on, and whether the difference is
worth a message.

No numpy and no sockets, so it is importable and testable anywhere. Same
door as a3_core_layout, a3_core_buttons and a3_core_crossfade.
"""

from math import isfinite


class _NoChange:
    """The answer "this is not worth sending", which is not a number.

    `__bool__` raises on purpose. A caller that writes `if wanted:` has asked
    the wrong question -- it would read "no change" and "a tempo of nothing"
    as the same thing, and the second one would set a delay line to a
    division by zero. Same device as a3_core_buttons.NO_CHANGE, and for the
    same reason: an answer that means "nothing to do" must not quietly
    collapse into a value.
    """

    def __bool__(self):
        raise TypeError(
            "NO_CHANGE is not a tempo -- compare with 'is NO_CHANGE'")

    def __repr__(self):
        return "NO_CHANGE"


#: Nothing to send.
NO_CHANGE = _NoChange()

#: How far the tempo has to move before it is worth a message, in BPM.
#:
#: 0.1 BPM at 130 is a third of a millisecond on a beat -- inaudible on a
#: delay, and far below the wobble of a live estimate. What it stops is the
#: chatter: in the analyzer's SYNTH mode the tempo is exactly constant and
#: nothing is sent after the first message at all.
#:
#: A deadband alone is **not** enough, and that was measured rather than
#: reasoned: on 2026-09-12 the analyzer's tempo did not wobble around a value
#: but ramped -- 107.7 down to 102.7 over four seconds, about a BPM per beat
#: -- and every single beat cleared this band. Thirty-four rewrites of the
#: delay line in a few seconds, which is the wobble the band exists to stop.
#: Hence DEFAULT_STEADY_BEATS below.
DEFAULT_DEADBAND = 0.1

#: How many beats a tempo has to hold still before it counts.
#:
#: One bar. Counted in beats rather than seconds on purpose: at half the
#: tempo the wait is twice as long in seconds and exactly as long musically,
#: which is the unit a tempo deserves to be judged in.
#:
#: While the tempo is on the move, nothing is sent at all and the delay keeps
#: the last tempo that meant something. That is the right failure: an echo
#: slightly behind the room is a mistake anyone can hear past, an echo whose
#: pitch slides is not.
DEFAULT_STEADY_BEATS = 4

#: What can be a tempo at all. Wider than the analyzer's own BPM_MIN/BPM_MAX
#: (60..140 in its .env) because a Pioneer deck or a tap can be outside that
#: range and this is not the place to have an opinion about music. It only
#: catches what cannot be a tempo: zero, negative, NaN, and numbers that
#: would set a delay line to something absurd.
BPM_MIN = 20.0
BPM_MAX = 400.0


class TempoFollower:
    """What to pass on, given what keeps arriving.

    Two numbers: the tempo last handed out, and the tempo currently being
    held still. A reading has to do both things before it is sent on -- hold
    still for a bar, and differ from what the far end already has.

    The deadband is measured against what was **sent** rather than against
    the previous reading, so a slow drift cannot creep past it a hundredth at
    a time.
    """

    def __init__(self, deadband=DEFAULT_DEADBAND,
                 steady_beats=DEFAULT_STEADY_BEATS):
        self._deadband = deadband
        self._steady_beats = steady_beats
        self._sent = None
        self._candidate = None
        self._run = 0

    def wanted(self, bpm):
        """The tempo to send on, or NO_CHANGE.

        A value that is not a tempo is refused, and refusing it changes
        nothing: it does not become what the deadband measures against, and
        it does not break a run of steady beats. One dropout in the middle of
        a settled tempo is not a tempo change.
        """
        try:
            bpm = float(bpm)
        except (TypeError, ValueError):
            return NO_CHANGE

        if not isfinite(bpm) or not (BPM_MIN <= bpm <= BPM_MAX):
            return NO_CHANGE

        if (self._candidate is None
                or abs(bpm - self._candidate) >= self._deadband):
            # A different tempo: the run starts again, at this one.
            self._candidate = bpm
            self._run = 1
            return NO_CHANGE

        self._run += 1
        if self._run < self._steady_beats:
            return NO_CHANGE

        if (self._sent is not None
                and abs(bpm - self._sent) < self._deadband):
            return NO_CHANGE

        self._sent = bpm
        return bpm
