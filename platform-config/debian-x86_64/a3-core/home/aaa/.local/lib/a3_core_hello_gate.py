"""Whose mixer values count: only those of a device that has said hello.

The truth comes from Core. After a restart a device that is still running
may speak before it has heard Core's replay -- a Motion once sent every mixer
value as 0.0 seconds after Core opened its output, and the rig went silent
(2026-10-14). So a host's values count only once it has said hello to this
Core run; what it sent before is ignored, and said once in the journal.

Keyed by host, because a value carries no device name, only a source
address. The one exception is Core's own replay of the evening, which comes
from a socket of its own: trusted by (host, port). Pure, no OSC: the caller
passes the address and does the logging.
"""

ADMIT = "admit"
REFUSE = "refuse"
REFUSE_FIRST = "refuse-first"


class HelloGate:
    def __init__(self):
        self._greeted = set()
        self._told = set()
        self._trusted = set()

    def greet(self, host):
        self._greeted.add(host)

    def trust(self, source):
        """Admit this exact (host, port) -- Core's own replay socket."""
        self._trusted.add(tuple(source))

    def check(self, host, port=None):
        """ADMIT, REFUSE, or REFUSE_FIRST -- the first refusal of a host, the
        one worth a line in the journal."""
        if (host, port) in self._trusted or host in self._greeted:
            return ADMIT
        if host in self._told:
            return REFUSE
        self._told.add(host)
        return REFUSE_FIRST
