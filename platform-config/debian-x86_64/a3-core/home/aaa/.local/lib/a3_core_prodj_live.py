# SPDX-FileCopyrightText: 2026 Patric Schmitz, Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Pro-DJ LIVE: whether a pro-DJ app may move the room, held as a lease.

Core starts with it off and never switches it on by itself. While a human
has it on, Core repeats it every REPEAT_EVERY seconds to the devices it
announces to, so a device that missed the "off" (restart, network drop)
falls back by its own 12 s timeout. Core switches it off when the last
prodj desk goes silent.

Pure: the caller passes the clock and does the sending. Read on the serve
thread only, so no lock.
"""

from a3_core_desks import PRODJ

REPEAT_EVERY = 5.0

#: Who may send /prodj/live at all. Motion only ever to off: the safety exit
#: where the DJ's hands are.
SWITCHERS = ("desk", "prodj", "motion")

# A prodj that is off may still say hello, ask for the recall and switch on.
ALWAYS_ADMITTED_FROM_A_PRODJ = frozenset(("device.hello", "state.recall", "prodj.live"))
POINTERS = frozenset(("stemdeck.pointer", "motion.pointer"))


class ProdjLive:
    def __init__(self):
        self._on = False
        self._last_sent = None

    @property
    def on(self):
        return self._on

    def set(self, wanted, now):
        """True when the state changed."""
        wanted = bool(wanted)
        if wanted == self._on:
            return False
        self._on = wanted
        self._last_sent = now if wanted else None
        return True

    def nobody_left(self):
        """Switch off because the last prodj went silent; True when it changed."""
        if not self._on:
            return False
        self._on = False
        self._last_sent = None
        return True

    def due(self, now):
        """While on: True once every REPEAT_EVERY seconds."""
        if not self._on or now - self._last_sent < REPEAT_EVERY:
            return False
        self._last_sent = now
        return True

    def admits(self, kind, key):
        """Whether a word from `kind` ("desk", "prodj", ... or None for an
        origin without a kind, such as Motion) may act.

        Only a prodj is held back, and only while off. The pointers are a
        prodj's alone, and only while on."""
        if key in POINTERS:
            return kind == PRODJ and self._on
        if kind != PRODJ:
            return True
        return self._on or key in ALWAYS_ADMITTED_FROM_A_PRODJ


def may_switch(origin_kind_or_name, wanted):
    if origin_kind_or_name == "motion":
        return not wanted
    return origin_kind_or_name in SWITCHERS
