# SPDX-FileCopyrightText: 2026 Patric Schmitz, Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Which virtual desks Core talks to: every one that said hello, four at most.

The A3 Mixer on a screen (a3-mixer's app/, spec virtual-desk) runs anywhere
on the network, and more than one may run: a second operator at the bar, a
stand-in for the hardware, later the pro-DJ app's hands (maintainer,
2026-10-16: several, Core caps them at four). Unlike Motion and StemDeck
there is no "only one": each desk that says hello with Core's own truth is
followed until it has been silent for a minute; a fifth is refused and said
once.

A desk is named by its host *and* the port it listens on, never by host
alone: a desk on a3nuc2 beside the Motion there sends from its desk.osc
socket, and Core must tell the two apart -- relay() leaves the sender out,
and leaving out "a3nuc2" would leave Motion dark.

Pure: the caller passes the clock and does the sending. A lock, because the
vu relay's thread reads the destinations while the serve thread changes
them.
"""

import threading

from a3_core_presence import Presence

DESK_SILENCE = 60.0
MAX_DESKS = 4

#: What a followed desk is: the virtual desk, or the pro-DJ app (which is a
#: desk that may also hold LIVE; a3_core_prodj_live).
DESK = "desk"
PRODJ = "prodj"

JOINED = "joined"
KNOWN = "known"
REFUSED_FIRST = "refused-first"
REFUSED = "refused"


def desk_name(host, kind=DESK):
    """What the window and relay() call the desk at `host`."""
    return f"{kind}@{host}"


def kind_of(name):
    """The part of a desk's name before the "@", None for a name without one
    (Motion's origin is the bare "motion")."""
    kind, at, _ = name.partition("@")
    return kind if at else None


class DeskFollowers:
    def __init__(self, port, vu_port, limit=MAX_DESKS, silence_after=DESK_SILENCE):
        self.port = port
        self._vu_port = vu_port
        self._limit = limit
        self._silence_after = silence_after
        self._followed = {}
        self._kinds = {}
        self._told_refused = set()
        self._lock = threading.Lock()

    def hello(self, host, now, kind=DESK):
        """JOINED for a desk that has just arrived (or come back after a
        silence), KNOWN for one already followed, REFUSED_FIRST and then
        REFUSED for one that would be one too many. A known host saying another kind
        takes the new kind."""
        with self._lock:
            presence = self._followed.get(host)
            if presence is not None:
                presence.heard(now)
                self._kinds[host] = kind
                return KNOWN
            if len(self._followed) >= self._limit:
                if host in self._told_refused:
                    return REFUSED
                self._told_refused.add(host)
                return REFUSED_FIRST
            presence = Presence(self._silence_after)
            presence.heard(now)
            self._followed[host] = presence
            self._kinds[host] = kind
            self._told_refused.discard(host)
            return JOINED

    def silence(self, now):
        """The hosts that have just gone quiet, each once; they are forgotten."""
        with self._lock:
            gone = [host for host, presence in self._followed.items()
                    if not presence.present(now)]
            for host in gone:
                del self._followed[host]
                del self._kinds[host]
            return gone

    def hosts(self):
        with self._lock:
            return list(self._followed)

    def hosts_of_kind(self, kind):
        with self._lock:
            return [host for host in self._followed if self._kinds[host] == kind]

    def name_of(self, host, port):
        """desk_name(host) when (host, port) is a followed desk's listening
        socket, else None."""
        with self._lock:
            if port == self.port and host in self._followed:
                return desk_name(host, self._kinds[host])
            return None

    def vu_destinations(self):
        with self._lock:
            return [(host, self._vu_port) for host in self._followed]
