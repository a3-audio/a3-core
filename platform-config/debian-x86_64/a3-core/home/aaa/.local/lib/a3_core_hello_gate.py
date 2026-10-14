"""Whose mixer values count: only those of a device that has said hello.

The truth comes from Core. After a restart a device that is still running
may speak before it has heard Core's replay -- a Motion once sent every mixer
value as 0.0 seconds after Core opened its output, and the rig went silent
(2026-10-14). So a host's values count only once it has said hello to this
Core run, with the fingerprint of Core's own a3-osc.json (a device on a stale
truth speaks words Core may not mean); what it sent before is ignored, and said once in the journal.

Keyed by host, because a value carries no device name, only a source
address. Two exceptions. Core's own replay of the evening comes from a
socket of its own: trusted by (host, port). And the desk at the truth's
hosts.mixer is trusted by host: it is in front, its values count from its
first message (2026-10-14) -- it says hello only every 30 s, and a reboot
locked it out for that long. Pure, no OSC: the caller passes the address and
does the logging.
"""

ADMIT = "admit"
REFUSE = "refuse"
REFUSE_FIRST = "refuse-first"
REFUSE_DIFFERS_FIRST = "refuse-differs-first"


class HelloGate:
    def __init__(self):
        self._greeted = set()
        self._told = set()
        self._trusted = set()
        self._trusted_hosts = set()
        self._differs = set()
        self._told_differs = set()

    def greet(self, host, matches):
        """A hello from `host`; `matches` is whether its truth fingerprint is
        Core's own. Only a match opens the host; a differing one closes it."""
        if matches:
            self._greeted.add(host)
            self._differs.discard(host)
            return
        self._greeted.discard(host)
        self._differs.add(host)
        self._told_differs.discard(host)

    def trust(self, source):
        """Admit this exact (host, port) -- Core's own replay socket."""
        self._trusted.add(tuple(source))

    def trust_host(self, host):
        """Admit every value from `host`, hello or not, whatever its
        fingerprint -- the desk at its configured address."""
        self._trusted_hosts.add(host)

    def check(self, host, port=None):
        """ADMIT, REFUSE, or REFUSE_FIRST -- the first refusal of a host, the
        one worth a line in the journal."""
        if (host in self._trusted_hosts or (host, port) in self._trusted
                or host in self._greeted):
            return ADMIT
        if host in self._differs:
            if host in self._told_differs:
                return REFUSE
            self._told_differs.add(host)
            return REFUSE_DIFFERS_FIRST
        if host in self._told:
            return REFUSE
        self._told.add(host)
        return REFUSE_FIRST
