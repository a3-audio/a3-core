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
#: **Not tuned for the detection mode.** With BTrack running, the estimate
#: wanders by around a BPM and most of that will get through this. Whether
#: that wants a wider band, or waiting for the tempo to settle, is a question
#: for someone who has listened to it -- guessing a number here would look
#: like a decision and be a shrug.
DEFAULT_DEADBAND = 0.1

#: What can be a tempo at all. Wider than the analyzer's own BPM_MIN/BPM_MAX
#: (60..140 in its .env) because a Pioneer deck or a tap can be outside that
#: range and this is not the place to have an opinion about music. It only
#: catches what cannot be a tempo: zero, negative, NaN, and numbers that
#: would set a delay line to something absurd.
BPM_MIN = 20.0
BPM_MAX = 400.0


class TempoFollower:
    """What to pass on, given what keeps arriving.

    Holds one number: the tempo last handed out. The deadband is measured
    against *that* rather than against the previous reading, so a slow drift
    cannot creep past it a hundredth at a time.
    """

    def __init__(self, deadband=DEFAULT_DEADBAND):
        self._deadband = deadband
        self._sent = None

    def wanted(self, bpm):
        """The tempo to send on, or NO_CHANGE.

        A value that is not a tempo is refused and, importantly, does not
        become what the deadband measures against: one stray message must not
        make the next honest one look like a change, nor hide it.
        """
        try:
            bpm = float(bpm)
        except (TypeError, ValueError):
            return NO_CHANGE

        if not isfinite(bpm) or not (BPM_MIN <= bpm <= BPM_MAX):
            return NO_CHANGE

        if self._sent is not None and abs(bpm - self._sent) < self._deadband:
            return NO_CHANGE

        self._sent = bpm
        return bpm
