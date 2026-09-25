"""Telling Core's own echo apart from news.

REAPER confirms everything it is told. Core sends a value, REAPER sends the
same value straight back, and a wire that relayed that would tell Motion what
Motion had just said.

That would be harmless if the trip were lossless. It is not: every value Core
sends is bent by a curve, coming back means bending it the other way, and
three of the eleven curves have plateaus where several inputs share an output.
On one of those the value would return changed, and the performer would watch
their own knob move under their hand.

So the echo is dropped and everything else is passed on. What is left is what
Core did not cause -- a fader moved in REAPER, and the whole state REAPER
dumps when the surface reconnects. Both are news, and the second one is what
recall is made of.

No sockets, no pythonosc: testable anywhere, like its two neighbours here.
"""

import time

#: How close counts as the same value.
#:
#: REAPER keeps floats of its own and a value can come back a ten-millionth
#: off; insisting on equality would let every echo through. The tolerance has
#: to stay well under a step anybody can make -- a fourteen-bit controller's
#: is about 0.00006 -- or a real move would be swallowed as an echo, which is
#: the worse of the two failures: a control that does nothing.
SAME_VALUE = 1e-6

#: How long after sending a disagreeing report counts as REAPER's old state.
#:
#: The stale reports measured on 2026-09-26 came within the same tenth of a
#: second as the value Core sent. Half a second covers that with room, and is
#: short enough that a real move in REAPER is at worst half a second late.
STALE_WINDOW = 0.5


class EchoFilter:
    """What Core last sent, per address, until it comes back once."""

    def __init__(self, tolerance=SAME_VALUE, stale_window=STALE_WINDOW,
                 clock=time.monotonic):
        self._tolerance = tolerance
        self._stale_window = stale_window
        self._clock = clock
        self._pending = {}

    def sent(self, address, value):
        """Core has just sent this; expect it back."""
        self._pending[address] = (value, self._clock())

    def is_echo(self, address, value):
        """True if this is the confirmation of what Core sent.

        Consumed when it matches: the same value arriving a second time is
        not an echo any more but part of REAPER's next state dump, and
        swallowing that would leave Motion holding whatever it had.
        """
        pending = self._pending.get(address)
        if pending is None:
            return False

        expected, _sent_at = pending
        if abs(expected - value) > self._tolerance:
            return False

        del self._pending[address]
        return True

    def is_stale(self, address, value):
        """True if this disagrees with what Core sent here moments ago.

        REAPER can report an address's old value in the same moment Core sets
        it; passed on, the old value reaches Motion and evening.json as if
        somebody had moved it back (#56). Only for `stale_window`: after that
        a different value is news again. The echo stays expected either way.
        """
        pending = self._pending.get(address)
        if pending is None:
            return False

        expected, sent_at = pending
        if abs(expected - value) <= self._tolerance:
            return False

        return self._clock() - sent_at < self._stale_window

    def forget(self, address=None):
        """Stop expecting an echo -- one address, or all of them.

        For a reconnect: what was expected before it is not coming, and a
        stale expectation would swallow the first real value on that address.
        """
        if address is None:
            self._pending.clear()
        else:
            self._pending.pop(address, None)
