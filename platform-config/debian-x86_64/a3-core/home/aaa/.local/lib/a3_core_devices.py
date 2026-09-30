"""Which truth each device speaks, and whether it is Core's own.

A device on a machine of its own -- the mixer -- reads a copy of a3-osc.json
put beside it at deploy. The day that copy goes stale it is a second truth,
and OSC over UDP would never say so. So the device names itself with the
sha256 of its copy (`/device/hello`), and this keeps what each one said, for
the window to show beside Core's own.
"""

import hashlib
import threading


def truth_hash(path):
    """The sha256 of the file's bytes: a copy made by cp or scp hashes the same."""
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


class Devices:
    def __init__(self, own_hash):
        self.own_hash = own_hash
        self._heard = {}
        self._lock = threading.Lock()

    def heard(self, device, their_hash, now):
        """Keep what `device` said. True if it is news -- the first word, or
        a different one from last time -- so the caller can log it once."""
        with self._lock:
            before = self._heard.get(device)
            self._heard[device] = (their_hash, now)
        return before is None or before[0] != their_hash

    def snapshot(self, now):
        with self._lock:
            heard = dict(self._heard)
        return [{"device": device, "hash": their_hash,
                 "matches": their_hash == self.own_hash, "age": now - at}
                for device, (their_hash, at) in sorted(heard.items())]
