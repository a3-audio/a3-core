"""What Core alone knows, kept across a restart.

Almost nothing needs this. Every continuous value -- gain, EQ, volume, 3d,
freq, Q -- passes through Core to REAPER and is held in the REAPER project,
which survives a restart because it is a project, and which reports every
change back of its own accord (see a3_core_reverse). A copy in Core would only
be a second answer to the same question, and it would drift the moment anybody
touched REAPER directly.

What is left is what has no REAPER parameter behind it: the three toggles a
channel carries and the filter mode. Those exist in Core's head and nowhere
else, so a restart loses them unless they are written down.

The module knows nothing about a3-core.py's dataclasses. It is handed them and
reads the fields it knows by name, which is what lets it be tested without
importing a3-core.py -- importing that opens sockets and starts a server.
"""

import json
import os
import threading
from pathlib import Path

#: The fields of a channel this remembers, and nothing else. A track number is
#: not the moment: it describes the rig and lives in layout.json.
#:
#: `elevation` and `width` are deliberately absent although ChannelInfo has
#: them. Nothing in a3-core.py writes either, and send_elevation(), the one
#: reader, is never called -- so remembering them would be keeping zeroes.
#: See issues/a3-core-elevation-cache-ist-tot.md; if that cache comes back,
#: this is the line that has to grow.
CHANNEL_FIELDS = ("toggle_fx", "toggle_pfl", "toggle_3d")

#: How long a change waits for the next one before it is written. A hand
#: sweeping a knob is one intention, and a file write in the path of every OSC
#: message is a file write in the path of the music.
DEFAULT_DELAY = 2.0


def state_of(channels, master):
    """Everything worth remembering, as data that json can write."""
    return {
        "channels": [{field: getattr(channel, field)
                      for field in CHANNEL_FIELDS}
                     for channel in channels],
        "fx_mode": master.fx_mode.value,
    }


def apply_state(state, channels, master):
    """Put a remembered state back, leaving alone anything it does not name.

    Every shape a file can arrive in is a shape it may arrive in: written by
    an older version that had a field fewer, by a newer one that has a field
    more, or by a device with a different number of channels. None of them may
    raise, and none of them may clear a value nobody asked about -- a state
    file is a convenience, and a convenience that stops the device from
    starting is worse than no state file at all.
    """
    for channel, remembered in zip(channels, state.get("channels", ())):
        for field in CHANNEL_FIELDS:
            if field in remembered:
                setattr(channel, field, remembered[field])

    if "fx_mode" in state:
        try:
            master.fx_mode = type(master.fx_mode)(state["fx_mode"])
        except ValueError:
            pass          # a mode this version does not have


class StateFile:
    """The state on disk: debounced going out, forgiving coming in.

    Written to a temporary name and moved into place, so a crash during the
    write leaves the previous file whole rather than half of a new one --
    os.replace is atomic within a filesystem.
    """

    def __init__(self, path, delay=DEFAULT_DELAY):
        self._path = Path(path)
        self._delay = delay
        self._lock = threading.Lock()
        self._pending = None
        self._last = None
        self._timer = None
        #: Counted so a test can say "that burst was one write" -- the whole
        #: point of the debounce is a number nothing else can observe.
        self.writes = 0

    def load(self):
        """What was remembered, or nothing at all.

        A file that is missing, unreadable or half-written is an empty state.
        Core comes up on its defaults in that case, which is what it did
        before there was a file.
        """
        try:
            return json.loads(self._path.read_text())
        except (OSError, ValueError):
            return {}

    def remember(self, state):
        """Note a state and start the clock on writing it, if it is new.

        A state equal to the last one offered is not a change. Every message
        that arrives offers one and almost none of them change it -- a fader
        sweep is hundreds -- and restarting the clock on each would push the
        write past the end of the sweep every time.
        """
        with self._lock:
            if state == self._last:
                return
            self._last = state
            self._pending = state
            if self._timer is not None:
                self._timer.cancel()
            self._timer = threading.Timer(self._delay, self.flush)
            self._timer.daemon = True
            self._timer.start()

    def flush(self):
        """Write now, if there is anything waiting. Idempotent."""
        with self._lock:
            if self._timer is not None:
                self._timer.cancel()
                self._timer = None
            state, self._pending = self._pending, None

        if state is None:
            return

        self._path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self._path.with_name(self._path.name + ".new")
        temporary.write_text(json.dumps(state, indent=2) + "\n")
        os.replace(temporary, self._path)
        self.writes += 1
