"""What went past Core, counted -- and nothing more on the hot path.

Core routes OSC between three devices over UDP, and UDP never answers. A
sender shouting into a dead port looks exactly like one that arrives; that is
how A3 Motion's mixer sent its fifteen addresses to the beat-analyzer for two
days without anyone seeing it. This is the thing that would have shown it, in
one line: a row saying the address had been seen zero times.

It is deliberately dull. `seen()` runs about a hundred times a second, so it
does a dict update and appends to a bounded ring, and that is all. No
formatting, no time series, no rate: the rate belongs to whoever reads two
snapshots, and a writer that kept one would be doing per-message work for a
reader who may not be there.

It replaces the `print()` a3-core.py does per message -- 301,385 lines of
`/channel/0/azimuth` in one hour, which is why searching the journal for
anything took minutes.
"""

import threading
import time
from collections import Counter, deque
from typing import Any, Dict, List

#: A message Core received.
IN = "in"

#: A message Core sent.
OUT = "out"

#: What the ring holds: about two minutes at a hundred messages a second,
#: which is the span in which "that was odd just now" is still worth looking
#: up. At roughly 150 bytes an entry that is about 1.5 MB, constant.
DEFAULT_HISTORY = 10000

#: What a host is called when more than one device claims it.
AMBIGUOUS = " (mehrdeutig)"


def peer_name(host: str, peers: Dict[str, str]) -> str:
    """The device name a host stands for, or the host itself.

    Only the host, never the port. Incoming messages carry an **ephemeral**
    source port -- `SimpleUDPClient` builds its socket without `bind()`, so
    the OS picks a fresh one per sender -- and matching on it would match
    nothing. On the rig the three devices are three hosts and this is exact.
    On a single box, where they share 127.0.0.1, it cannot tell them apart
    and says so rather than guessing: a wrong label is worse than none.
    """
    matches = sorted(name for name, at in peers.items() if at == host)

    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        return host + AMBIGUOUS
    return host


class Traffic:
    """Every message Core has seen, by address and direction."""

    def __init__(self, history: int = DEFAULT_HISTORY) -> None:
        self._lock = threading.Lock()
        self._rows: Dict[tuple, Dict[str, Any]] = {}
        self._history: deque = deque(maxlen=history)
        self._unhandled: Counter = Counter()

    def seen(self, direction: str, address: str, value: Any,
             peer: str) -> None:
        """Note one message. Called from every OSC thread."""
        at = time.monotonic()
        key = (direction, address)
        # The type is kept beside the value because in this rig it carries
        # meaning: a string "1" is the A3 Mixer's momentary edge, a float 1.0
        # is A3 Motion's state. See a3_core_buttons.
        type_name = type(value).__name__

        with self._lock:
            row = self._rows.get(key)
            if row is None:
                row = {"direction": direction, "address": address,
                       "count": 0, "last_value": None, "last_type": "",
                       "last_seen": at, "peer": peer}
                self._rows[key] = row

            row["count"] += 1
            row["last_value"] = value
            row["last_type"] = type_name
            row["last_seen"] = at
            row["peer"] = peer

            self._history.append({"at": at, "direction": direction,
                                  "address": address, "value": value,
                                  "type": type_name, "peer": peer})

    def unhandled(self, address: str) -> None:
        """Note something that arrived and nothing knew how to pass on."""
        with self._lock:
            self._unhandled[address] += 1

    def snapshot(self) -> Dict[str, Any]:
        """A whole, consistent picture, safe to hand to another thread.

        The rows are copied rather than handed out: a reader walking a dict
        that the OSC threads are still writing into is the one way this could
        take the rig down, and copies of a few hundred small dicts cost
        nothing beside that.
        """
        with self._lock:
            rows: List[Dict[str, Any]] = [dict(row)
                                          for row in self._rows.values()]
            history = [dict(entry) for entry in self._history]
            unhandled = dict(self._unhandled)

        return {"at": time.monotonic(), "rows": rows, "history": history,
                "unhandled": unhandled}
