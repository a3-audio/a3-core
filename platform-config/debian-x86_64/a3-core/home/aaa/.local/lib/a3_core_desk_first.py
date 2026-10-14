"""The desk beats Core's own evening replay.

The desk (A3 Mixer at the truth's hosts.mixer) is in front when it is there
(maintainer, 2026-10-14): on a reboot it sends every position it holds as
soon as it hears Core, and that may arrive before Core's replay of the
evening or after it. Both orders must end with the desk's values.

After it is the ordinary case: a touch overwrites what was replayed. Before
it is not: the replay would overwrite the desk. And skipping those addresses
in the replay is not enough -- what the desk sent before REAPER listened
went nowhere, so REAPER would keep its template's value. So the replay
carries the desk's value instead of the evening's, for every address the
desk has sent in this Core run, on every replay of the run (Core's start
and each REAPER restart).

Between the desk and Motion the latest touch wins, as everywhere: a later
value from anyone on an address the desk has sent replaces the desk's. An
address only Motion has touched keeps today's rule and is replayed from the
evening. Pure; a lock because the receive thread hears and the replay
thread reads.
"""

import threading


class DeskFirst:
    def __init__(self):
        self._values = {}
        self._lock = threading.Lock()

    def heard(self, address, value, from_desk):
        """A value that passed through Core's door this run."""
        with self._lock:
            if from_desk or address in self._values:
                self._values[address] = value

    def over(self, evening):
        """`evening`, (address, value) pairs, with the desk's values laid
        over it: replaced where the evening has the address, added where it
        does not."""
        merged = dict(evening)
        with self._lock:
            merged.update(self._values)
        return list(merged.items())
