"""Which Motion Core talks to: the rig's own, or the one that arrived last.

A Motion on another machine (a3nuc2, the notebook) runs *instead of* the
rig's, never beside it (maintainer, 2026-10-05; spec devices-and-remote-access).
It says hello like the desk and StemDeck do, and Core points its Motion target
at the sender; a minute of silence and the rig's own is the target again. No
configuration to switch: start Motion where you want to play.

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
        self._watch = HelloWatch(silence_after, is_local=lambda host: host == LOCAL)

    @property
    def remote(self):
        return self.current != self.default

    def hello(self, host, now):
        """The new (host, port) when this hello moves the target, else None."""
        local = is_this_machine(host, self._own_hosts)
        if not self._watch.hello(LOCAL if local else host, now):
            return None
        return self._point_at(self.default if local
                              else (host, self._remote_port))

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

    def _point_at(self, endpoint):
        if endpoint == self.current:
            return None
        self.current = endpoint
        return endpoint
