"""The list of addresses Core has seen, kept across a restart.

Core was restarted six times on 2026-09-12 and the window forgot everything
each time: 19,335 unknown addresses in the morning, five by 13:20. REAPER
counts its vocabulary out once, when its OSC surface connects, and never
again -- so a Core that comes up afterwards sees an all but empty table and
has no way to ask for the rest.

**The list, not the history.** `a3_core_traffic`'s ring is two minutes of what
just happened and was never meant as an archive. Restoring it would be
restoring a claim about *now* out of a process that is dead.

**And not the values either.** A restored row has a count, a peer and a time,
and no `last_value`. The number a knob stood at before the restart says
nothing about where it stands now, and sitting in the value column it would be
indistinguishable from a live one. What answers "where is it now" is the
recall -- see a3_core_recall.

**Never on the hot path.** `Traffic.seen()` runs about a hundred times a
second; a file access in there is a file access in the way of the music. So
nothing here is called from it. Instead `Traffic.created()` counts addresses
taken in -- one increment, under a lock already held, on the rare branch that
creates a row -- and the thread this module starts watches that number. When
it has stopped moving, the list is written. That is `a3_core_state`'s
debounce with the clock on the other side: there, a change arms a timer; here,
a quiet interval triggers the write, because the burst that matters is REAPER
reporting nineteen thousand addresses at once and the point is to write after
it rather than four times during it.

**What it costs, measured rather than assumed** (this machine, 2026-09-12):

    19,335 unknown addresses   1.27 MB   24 ms to write
    50,000 (the table's cap)   3.29 MB   62 ms to write

plus 13 ms inside Traffic's lock to copy the unknown table at 19,335 rows.
Those milliseconds are the reason for the debounce and the reason rows are
written as lists rather than objects -- as objects the same tables are 2.07
and 5.34 MB and half again as slow. They are OSC-routing latency, not audio:
the sound is made by JACK and SuperCollider in other processes.

**A broken file is not a reason to fall.** Same rule as
`a3_core_state.StateFile`: missing, half-written, from another version or not
a register at all, every one of them restores nothing and raises nothing.
"""

import json
import os
import threading
import time
from pathlib import Path

#: How long the address list has to stand still before it is written. Longer
#: than a3_core_state's two seconds on purpose: what starts a write here is
#: REAPER's surface reporting its whole vocabulary, which takes seconds, and
#: five means that burst is one file rather than three.
DEFAULT_DELAY = 5.0

#: The shape of a saved row, in order. A list rather than an object per row:
#: at the unknown table's cap that is 3.29 MB instead of 5.34 MB and 62 ms
#: instead of 93 -- measured, see the module docstring.
ROW_FIELDS = ("direction", "address", "count", "peer", "last_seen")
UNKNOWN_FIELDS = ("address", "count", "peer", "last_seen")

#: Bumped when the shape above changes. A file from another version restores
#: nothing rather than restoring nonsense.
VERSION = 1


def state_path():
    """Where the list lives: under XDG's state directory, beside state.json.

    Not beside the register in the package. The register describes what the
    system can speak and ships with it; this is what one evening's traffic
    happened to be, and it must survive an update untouched -- the same
    reasoning as a3-core.py's STATE_PATH.
    """
    return (Path(os.environ.get("XDG_STATE_HOME",
                                Path.home() / ".local/state"))
            / "a3-core/seen.json")


class SeenFile:
    """The address list on disk: debounced going out, forgiving coming in."""

    def __init__(self, path, traffic, delay=DEFAULT_DELAY):
        self._path = Path(path)
        self._traffic = traffic
        self._delay = delay
        self._thread = None
        self._stop = threading.Event()
        self._written_at_created = None
        self._polled = None
        #: Counted so a test can say "that burst was one write" -- the whole
        #: point of the debounce is a number nothing else can observe.
        self.writes = 0
        #: What went wrong the last time a write was tried, or "". A window
        #: that cannot save is worth knowing about and is not worth stopping
        #: for.
        self.problem = ""

    # ------------------------------------------------------------ coming in

    def restore(self):
        """Put the saved list back into the traffic. Returns how many rows.

        The saved times are wall-clock and the traffic's are monotonic,
        which is the whole reason this conversion is here and not in
        `Traffic.restore`: a monotonic clock does not survive a process, let
        alone a reboot, and the age column is the one thing a reader uses to
        tell a live wire from a dead one. So the file records what the wall
        clock said, and the elapsed real time is subtracted from this
        process's monotonic now.

        A file from the future -- a clock that was corrected backwards, or a
        file copied off another machine -- would otherwise produce rows
        younger than the moment they are read in. Those are clamped to now,
        which reads as "just seen" and is the mild lie; the alternative is a
        negative age and an age column full of nonsense.
        """
        saved = self._read()
        if saved is None:
            return 0

        now_wall, now_mono = time.time(), time.monotonic()
        elapsed = now_wall - saved.get("at", now_wall)

        def when(stamp):
            behind = max(0.0, elapsed + (saved.get("at", now_wall) - stamp))
            return now_mono - behind

        rows = [{"direction": item[0], "address": item[1], "count": item[2],
                 "peer": item[3], "last_seen": when(item[4])}
                for item in _usable(saved.get("rows"), len(ROW_FIELDS), 1)]
        unknown = [{"address": item[0], "count": item[1], "peer": item[2],
                    "last_seen": when(item[3])}
                   for item in _usable(saved.get("unknown"),
                                       len(UNKNOWN_FIELDS), 0)]

        return self._traffic.restore(rows, unknown)

    def _read(self):
        try:
            saved = json.loads(self._path.read_text())
        except (OSError, ValueError):
            return None
        if not isinstance(saved, dict) or saved.get("version") != VERSION:
            return None
        return saved

    # ------------------------------------------------------------ going out

    def write(self):
        """Write the list now. Never raises."""
        snapshot = self._traffic.snapshot(history_since=time.monotonic())
        unknown = self._traffic.unknown_snapshot()
        created = self._traffic.created()

        # Both snapshots stamp an `at` from the monotonic clock; the file has
        # to carry wall-clock times, so each row's age is measured against the
        # snapshot it came from and then subtracted from now. Two snapshots
        # means two reference points, which is why this is not one constant.
        now = time.time()

        def stamp(row, at):
            return now - max(0.0, at - row["last_seen"])

        saved = {
            "version": VERSION,
            "at": now,
            "rows": [[row["direction"], row["address"], row["count"],
                      row["peer"], stamp(row, snapshot["at"])]
                     for row in snapshot["rows"]],
            "unknown": [[row["address"], row["count"], row["peer"],
                         stamp(row, unknown["at"])]
                        for row in unknown["rows"]],
        }

        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self._path.with_name(self._path.name + ".new")
            temporary.write_text(json.dumps(saved))
            # Atomic within a filesystem, so a crash mid-write leaves the
            # previous file whole rather than half of a new one. Same as
            # a3_core_state.StateFile.
            os.replace(temporary, self._path)
        except OSError as problem:
            self.problem = str(problem)
            return

        self.problem = ""
        self.writes += 1
        self._written_at_created = created

    def follow(self):
        """Watch the address list on a daemon thread, and write when it
        settles.

        Polling rather than a callback out of `seen()`: a callback would put
        this module's lock inside Traffic's, and arming a timer a hundred
        times a second would be a thread a second for a file that changes a
        few times an evening.
        """
        if self._thread is not None:
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._watch, daemon=True)
        self._thread.start()

    def stop(self):
        """For the tests, and for a clean shutdown. Idempotent."""
        self._stop.set()
        thread, self._thread = self._thread, None
        if thread is not None:
            thread.join(timeout=self._delay * 2 + 1.0)

    def _watch(self):
        while not self._stop.wait(self._delay):
            created = self._traffic.created()
            # Two conditions, and both are needed. The list has to have
            # changed since the last write, and it has to have stopped
            # changing since the last look -- otherwise REAPER's nineteen
            # thousand addresses would be written once per poll all the way
            # through the burst instead of once at the end of it.
            settled = created == self._polled
            self._polled = created
            if settled and created != self._written_at_created:
                self.write()


def _usable(rows, width, address_at):
    """The rows of a saved list that can be read, skipping the ones that
    cannot.

    One unreadable row out of nineteen thousand must not cost the other
    nineteen thousand. Every shape a file can arrive in is a shape it may
    arrive in -- written by a version with a field more or fewer, truncated,
    or hand-edited.
    """
    if not isinstance(rows, list):
        return []

    good = []
    for row in rows:
        if not isinstance(row, list) or len(row) != width:
            continue
        if not isinstance(row[address_at], str) or not row[address_at]:
            continue
        if not isinstance(row[address_at + 1], int):
            continue
        if not isinstance(row[width - 1], (int, float)):
            continue
        good.append(row)
    return good
