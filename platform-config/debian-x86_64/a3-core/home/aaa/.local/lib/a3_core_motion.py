"""Which Motion Core talks to: the one that said hello first and is still there.

Only one Motion may run (maintainer, 2026-10-14): Core follows the first and
reports a second once (HelloWatch one_at_a_time); when the followed one has
been silent for a minute the next hello takes over, and a silent remote falls
back to the rig's own. This replaces the 2026-10-06 rule that a remote Motion
takes over from the rig's own. A Motion on another machine (a3nuc2, the
notebook) says hello like the desk and StemDeck do; no configuration to
switch: start Motion where you want to play.

The rig's own Motion is recognised by its address, not by its word: a hello
from loopback or from one of Core's own addresses is the local one, and its
target is the default -- the truth's motion.osc, or --motion on a bench.

Pure: the caller passes the clock and does the sending.
"""

import ipaddress

from a3_core_presence import MOTION_SILENCE, HelloWatch

#: How the watch names the rig's own Motion, whichever of this machine's
#: addresses it happened to send from.
LOCAL = "local"


def is_this_machine(host, own_hosts):
    """Loopback, or one of the addresses Core knows as its own. A name is
    compared as written: an incoming packet carries a number."""
    try:
        if ipaddress.ip_address(host).is_loopback:
            return True
    except ValueError:
        pass
    return host in own_hosts


class MotionTarget:
    def __init__(self, default, remote_port, own_hosts, vu_port,
                 silence_after=MOTION_SILENCE):
        self.default = tuple(default)
        self.current = self.default
        self._remote_port = remote_port
        self._vu_port = vu_port
        self._own_hosts = frozenset(own_hosts)
        self._watch = HelloWatch(silence_after, is_local=lambda host: host == LOCAL,
                                 one_at_a_time=True)
        self._seen_as = {}

    @property
    def remote(self):
        return self.current != self.default

    def hello(self, host, now):
        """The new (host, port) when this hello moves the target, else None."""
        local = is_this_machine(host, self._own_hosts)
        key = LOCAL if local else host
        self._seen_as[key] = host
        if not self._watch.hello(key, now):
            return None
        return self._point_at(self.default if local
                              else (host, self._remote_port))

    @property
    def followed(self):
        """The address of the Motion followed, as it was heard; None if none."""
        return self._seen_as.get(self._watch.host)

    def take_rivals(self):
        """Addresses of Motions that turned up beside the one followed, each once."""
        return [self._seen_as.get(key, key) for key in self._watch.take_rivals()]

    def silence(self, now):
        """The default, once, when the remote Motion followed has gone quiet."""
        if not self._watch.silence(now):
            return None
        return self._point_at(self.default)

    def vu_destination(self):
        """Where Core forwards the analyzer's meters: the remote Motion's vu
        port, or None -- the rig's own gets them from the analyzer directly."""
        if not self.remote:
            return None
        return self.current[0], self._vu_port

    def forwards_beat(self):
        """Whether Core passes the analyzer's /beat on to the Motion followed:
        only a remote one -- the analyzer reaches the rig's own directly."""
        return self.remote

    def _point_at(self, endpoint):
        if endpoint == self.current:
            return None
        self.current = endpoint
        return endpoint
