"""What went past Core, counted -- and nothing more on the hot path.

Core routes OSC between three devices over UDP, and UDP never answers. A
sender shouting into a dead port looks exactly like one that arrives; that is
how A3 Motion's mixer sent its fifteen addresses to the beat-analyzer for two
days without anyone seeing it. This is the thing that would have shown it --
not a row saying zero, but an absence: `seen()` creates a row and increments
it in the same step, so an address nobody has sent has no row at all. The
table would read `/channel/0/azimuth` at 301,385 with `/channel/0/gain`
missing entirely rather than sitting at 0. An absence is harder to notice
than a zero would have been, which is why the list of expected addresses this
module said it did not have now exists: `a3_core_register` holds the
catalogue, generated from `layout.json`, `OscAddresses.hh` and four more
sources, and says which of its addresses have never arrived. Nothing of that
is in here -- this is still only a counter -- but "expected but never seen" is
answerable now, one fetch away.

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
from collections import OrderedDict, deque
from typing import Any, Dict, Iterable, List, Optional

#: A message Core received.
IN = "in"

#: A message Core sent.
OUT = "out"

#: What the ring holds: about two minutes at a hundred messages a second,
#: which is the span in which "that was odd just now" is still worth looking
#: up. At roughly 150 bytes an entry that is about 1.5 MB, constant.
DEFAULT_HISTORY = 10000

#: How many understood addresses are kept. Generous on purpose: A3's own
#: vocabulary is about forty, so the cap is a guard against a future sender
#: inventing addresses, not a working limit.
DEFAULT_ROW_CAP = 5000

#: How many unknown addresses are kept. The known real figure is 19,335 --
#: what one REAPER project reports when its OSC surface connects -- so this
#: holds a little over twice that. Eviction is least-recently-seen, and the
#: count of what fell out is reported rather than hidden.
DEFAULT_UNKNOWN_CAP = 50000

#: What a host is called when more than one device claims it.
AMBIGUOUS = " (mehrdeutig)"

#: The devices that can have sent a message on Core's main port, and the ones
#: that can have sent on REAPER's feedback port. They are disjoint, which is
#: the whole point: pass the matching one as `only` and a shared host stops
#: being ambiguous. See peer_name.
COMMANDERS = ("mixer", "motion")
ANSWERERS = ("reaper",)


def peer_name(host: str, peers: Dict[str, str],
              only: Optional[Iterable[str]] = None) -> str:
    """The device name a host stands for, or the host itself.

    Never the **source** port: incoming messages carry an ephemeral one --
    `SimpleUDPClient` builds its socket without `bind()`, so the OS picks a
    fresh one per sender -- and matching on it would match nothing.

    But Core's **own receiving** port is not ephemeral, and it does carry an
    identity: REAPER answers on the feedback port and never commands on the
    main one, the two controllers command on the main port and never answer
    on the feedback one. `only` is how a caller says which port this message
    arrived at -- `COMMANDERS` or `ANSWERERS` -- and the candidates are
    narrowed to those before the host is compared.

    That narrowing is what makes a shared host readable. With all three in
    one pool, Motion running on Core itself collided with REAPER's own
    127.0.0.1 and every incoming message read "127.0.0.1 (mehrdeutig)" --
    which is how the window came to show Motion's messages without ever
    naming Motion. Narrowing is not guessing, though: the one tie no port can
    break is both controllers on one box, and that stays ambiguous.

    Omitting `only` keeps the old all-three behaviour, for a caller that does
    not know the port.

    `peers` (`PEER_HOSTS` in a3-core.py) is built from the host part of
    whatever was passed to --mixer/--motion/--reaper, taken as written on
    the command line. If one of those is given as a hostname rather than an
    IP, it is compared as text against the numeric address an incoming UDP
    packet actually carries, and a hostname never equals an IP -- so that
    peer never matches here and the table quietly falls back to showing the
    raw address instead of the device name.
    """
    if only is not None:
        allowed = set(only)
        peers = {name: at for name, at in peers.items() if name in allowed}

    matches = sorted(name for name, at in peers.items() if at == host)

    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        return host + AMBIGUOUS
    return host


class Traffic:
    """Every message Core has seen, by address and direction."""

    def __init__(self, history: int = DEFAULT_HISTORY,
                 row_cap: int = DEFAULT_ROW_CAP,
                 unknown_cap: int = DEFAULT_UNKNOWN_CAP) -> None:
        self._lock = threading.Lock()
        # OrderedDict rather than dict: eviction has to drop the
        # least-recently-seen entry, which means re-ordering a key to the
        # front on every touch, not just on insert. A plain dict keeps
        # insertion order too and would hand back its oldest key just as
        # cheaply with next(iter(d)) -- what it cannot do cheaply is move an
        # existing key when it is touched again, short of a delete plus a
        # reinsert. move_to_end + popitem(last=False) do the whole cycle in
        # constant time, on a machine that is making sound.
        self._rows: "OrderedDict[tuple, Dict[str, Any]]" = OrderedDict()
        self._unknown: "OrderedDict[str, Dict[str, Any]]" = OrderedDict()
        self._history: deque = deque(maxlen=history)
        self._row_cap = row_cap
        self._unknown_cap = unknown_cap
        self._evicted_rows = 0
        self._evicted_unknown = 0
        # How many rows either table has ever created, counting one per new
        # address rather than per message. It only ever grows, eviction
        # included.
        #
        # This is what a writer watches to know the *list* has changed, which
        # is the one thing worth putting on disk (a3_core_seen). A count that
        # ticked up on an address already in the file would be a write for
        # nothing, and at a hundred messages a second that is every message.
        # Incremented under this lock on the rare branch that creates a row,
        # never on the path a repeat message takes.
        self._created = 0
        # Messages ever received on an address Core could not route -- not
        # "messages currently sitting in _unknown", which is what summing
        # each row's count would mean once eviction starts dropping rows. A
        # row leaving the table on eviction does not un-happen the messages
        # that made up its count, and "N Nachrichten" on the page reads like
        # a lifetime total, not a residual one -- so this never decreases,
        # only grows, kept beside `unknown()`'s own increment rather than
        # recomputed by summing the table on every snapshot (see there).
        self._unknown_messages_total = 0

    def seen(self, direction: str, address: str, value: Any,
             peer: str) -> None:
        """Note one message. Called from every OSC thread."""
        key = (direction, address)
        # The type is kept beside the value because in this rig it carries
        # meaning: a string "1" is the A3 Mixer's momentary edge, a float 1.0
        # is A3 Motion's state. See a3_core_buttons.
        type_name = type(value).__name__

        with self._lock:
            # Stamped here, not before the lock: `snapshot()` also stamps
            # its `at` first thing inside the same lock, and the delta
            # stream (history_since) relies on `at` ordering matching
            # lock-acquisition ordering. If this entry's `at` were taken
            # before the lock, a snapshot could acquire the lock first,
            # stamp a later `at`, and copy the ring -- all before this
            # entry is appended -- and this entry would then read as "not
            # newer" than every future cutoff and never be sent, ever.
            at = time.monotonic()
            row = self._rows.get(key)
            if row is None:
                row = {"direction": direction, "address": address,
                       "count": 0, "last_value": None, "last_type": "",
                       "last_seen": at, "peer": peer}
                self._rows[key] = row
                self._created += 1

                # An address Core could not route before and can now. The
                # unknown table survives a restart (a3_core_seen) while what
                # Core can route changes between restarts -- the reverse
                # table grew on 2026-09-12 and twenty-three addresses moved
                # across, leaving their old rows behind. The window then
                # listed them in both tables, and somebody hunting a dead
                # wire would have found a routed address called
                # unrecognised.
                #
                # Only on the branch that creates a row, and only inbound.
                # seen() runs about a hundred times a second and a repeat
                # message must not pay for a lookup it can never need; and
                # `unknown()` is called from the feedback path alone, so an
                # *outgoing* row says nothing about whether the same address
                # is understood coming back. Core sends
                # /track/n/fx/1/fxparam/1/value and cannot read the crossfade
                # back out of it -- known outbound, unknown inbound, and that
                # pair is the truth rather than a leftover.
                if direction == IN and address in self._unknown:
                    del self._unknown[address]

            row["count"] += 1
            row["last_value"] = value
            row["last_type"] = type_name
            row["last_seen"] = at
            row["peer"] = peer

            self._rows.move_to_end(key)
            while len(self._rows) > self._row_cap:
                self._rows.popitem(last=False)
                self._evicted_rows += 1

            self._history.append({"at": at, "direction": direction,
                                  "address": address, "value": value,
                                  "type": type_name, "peer": peer})

    def unknown(self, address: str, value: Any, peer: str) -> None:
        """Note a message Core received and could not route.

        Keeps a full row rather than a bare count, because the point of the
        table is to answer "what is REAPER saying that we ignore" -- and that
        question wants the value and the time as much as the name.

        Deliberately NOT in `snapshot()`. One REAPER project reports 19,335
        distinct addresses the moment its surface connects; carrying those in a
        snapshot that a stream pushes four times a second is what froze the
        maintainer's machine on 2026-09-10. They are fetched through
        `unknown_snapshot()` when somebody asks.
        """
        with self._lock:
            at = time.monotonic()
            row = self._unknown.get(address)
            if row is None:
                row = {"address": address, "count": 0, "last_value": None,
                       "last_type": "", "last_seen": at, "peer": peer}
                self._unknown[address] = row
                self._created += 1

            row["count"] += 1
            row["last_value"] = value
            row["last_type"] = type(value).__name__
            row["last_seen"] = at
            row["peer"] = peer
            # Kept beside the row's own count, not derived by summing the
            # table in snapshot(): that sum costs 0.024 ms empty, 3.51 ms at
            # this rig's 19,335 real unknown addresses, and 8.79 ms at the
            # 50,000 cap -- paid on every snapshot, four times a second, per
            # open tab, inside the very lock every OSC thread has to take.
            self._unknown_messages_total += 1

            self._unknown.move_to_end(address)
            while len(self._unknown) > self._unknown_cap:
                self._unknown.popitem(last=False)
                self._evicted_unknown += 1

    def snapshot(self,
                 history_since: Optional[float] = None) -> Dict[str, Any]:
        """A whole, consistent picture, safe to hand to another thread.

        The rows are copied rather than handed out: a reader walking a dict
        that the OSC threads are still writing into is the one way this could
        take the rig down, and copying the understood addresses -- about
        forty of them in this rig -- costs nothing beside that.

        `history_since` makes `history` incremental instead of complete. Rows
        stay whole every time regardless -- they are the addresses Core
        understood, about forty of them in this rig, so copying all of them
        is cheap, and a reader needs the current count for each one, not a
        delta. The unknown table is not here at all: REAPER alone can put
        19,335 addresses in it, and that is the table whose full copy on
        every stream tick froze the maintainer's machine -- see `unknown()`.
        It is fetched separately, through `unknown_snapshot()`, only when
        somebody asks.

        History is different: at a hundred messages a second the ring holds
        ten thousand entries, and a stream ticking four times a second that
        resent all of them every tick would cost more than the `print()` per
        message this feature exists to replace. A caller that already has an
        earlier snapshot's `at` passes it here and gets only what happened
        since.

        The invariant `history_since` depends on: an entry belongs to this
        snapshot's copy if its own `at` is not greater than this snapshot's
        `at`, and is excluded otherwise. That only holds if `at` is stamped
        here, inside the lock, rather than after it -- otherwise `seen()`
        could append an entry between this method releasing the lock and it
        stamping `at` outside, and that entry would read as newer than a
        snapshot it is actually part of, or be excluded from a copy taken
        before it existed while carrying an `at` that is not later than that
        copy's. Either way the next delta's cutoff would be wrong. Taking it
        as the first thing inside the lock, matching `seen()`, ties the
        order of `at` values to the order of lock acquisitions, which is the
        only ordering that is actually true.

        This does not make the two directions of that invariant equally
        certain. "Excluded implies later" is exact -- `history_since`
        compares with `>`. "Included implies not later" rests on an
        assumption rather than a proof: it also requires that no entry's
        `at` ever *ties* a snapshot's, since a tie would read as "later" to
        `>` and drop that entry from every future delta. A tie needs two
        `time.monotonic()` reads, separated by a lock acquire and release,
        to return the identical value -- on Linux's nanosecond-resolution
        monotonic clock that is not something a proof rules out, only
        something this codebase has never observed. Making it exact would
        mean cutting on a sequence number incremented inside the lock
        instead of on a clock reading, which removes the assumption
        entirely; deliberately not done here.
        """
        with self._lock:
            # Stamped inside the lock -- see the invariant explained above.
            at = time.monotonic()
            rows: List[Dict[str, Any]] = [dict(row)
                                          for row in self._rows.values()]
            if history_since is None:
                history = [dict(entry) for entry in self._history]
            else:
                history = [dict(entry) for entry in self._history
                          if entry["at"] > history_since]
            unknown_addresses = len(self._unknown)
            # A running total kept by unknown() itself, not summed here: see
            # the comment on _unknown_messages_total in __init__ for the
            # eviction semantics, and on the increment in unknown() for the
            # cost this avoids paying under this lock four times a second.
            unknown_messages = self._unknown_messages_total
            evicted = {"rows": self._evicted_rows,
                       "unknown": self._evicted_unknown}

        return {"at": at, "rows": rows, "history": history,
                "unknown_addresses": unknown_addresses,
                "unknown_messages": unknown_messages, "evicted": evicted}

    def created(self) -> int:
        """How many addresses either table has ever taken in.

        Grows by one per address the first time it is seen and never
        shrinks, so two readings that differ mean "the list is not what it
        was" -- which is the question a3_core_seen asks before it writes a
        file. Two readings that agree mean the list has not grown, even
        though the counts inside it may have moved a great deal.
        """
        with self._lock:
            return self._created

    def restore(self, rows: Iterable[Dict[str, Any]],
                unknown: Iterable[Dict[str, Any]]) -> int:
        """Put a saved address list back, without overwriting what is live.

        Called once at start-up, from a3_core_seen. A row that is already
        here wins: restoring happens while the OSC servers are already
        listening, and what is arriving now is never less true than what was
        saved yesterday.

        The values are deliberately not part of this -- a restored row has no
        `last_value` and no `last_type`. `last_seen` arrives already converted
        into this process's monotonic clock; see a3_core_seen for why that
        conversion cannot be done here.
        """
        put = 0
        with self._lock:
            for row in rows:
                key = (row["direction"], row["address"])
                if key in self._rows:
                    continue
                self._rows[key] = dict(row, last_value=None, last_type="")
                self._created += 1
                put += 1
            while len(self._rows) > self._row_cap:
                self._rows.popitem(last=False)
                self._evicted_rows += 1

            for row in unknown:
                if row["address"] in self._unknown:
                    continue
                self._unknown[row["address"]] = dict(row, last_value=None,
                                                     last_type="")
                # The running total is a lifetime figure the page shows beside
                # the address count ("N Adressen, M Nachrichten"). Restoring
                # the rows without it would put a table of 19,335 addresses
                # beside a total of nought.
                self._unknown_messages_total += row["count"]
                self._created += 1
                put += 1
            while len(self._unknown) > self._unknown_cap:
                self._unknown.popitem(last=False)
                self._evicted_unknown += 1

        return put

    def unknown_snapshot(self) -> Dict[str, Any]:
        """The whole unknown table, for a caller that asked for it.

        Separate from `snapshot()` on purpose: this one can be tens of
        thousands of rows, and nothing should put it on a four-times-a-second
        stream. The rows share every field a reader needs with `snapshot()`'s
        rows, but not `"direction"`: Core only ever *receives* what it
        cannot route, so there is no second direction here to distinguish it
        from, and the field is left out rather than hardcoded to a constant.
        A caller that wants to draw both tables with one function adds it
        back in.
        """
        with self._lock:
            at = time.monotonic()
            rows = [dict(row) for row in self._unknown.values()]
            evicted = self._evicted_unknown
        return {"at": at, "rows": rows, "evicted": evicted}
