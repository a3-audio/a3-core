"""The silent start: REAPER's outputs open after the recall (a3-system#74).

Decided 2026-10-07. REAPER's template starts main, booth and phones muted, so
a cold start is silent instead of playing the template's levels for the
seconds before Core's recall lands. Core opens them once the recall has been
applied: fader to -inf, unmute, then 50 steps of 20 ms back to the fader
REAPER reported -- REAPER's OSC has no ramp, so Core steps it, linear in
REAPER's normalized fader value, which its fader curve makes roughly even in
dB.

Core asks, it does not remember: only tracks REAPER *reports* muted are
opened. A REAPER that reports them open is a running set (Core restarted on
its own), and Core sends it nothing. No report, no fader, no recall: the gate
stays shut. That is the intended failure -- a silent rig, never a loud one.

No sockets here; a3-core.py hands in the sender, the printer and the sleep.
"""

import secrets
import time
from collections import namedtuple

FADE_SECONDS = 1.0
FADE_STEPS = 50
#: After the recall's last message leaves Core, before the fade starts: time
#: for REAPER to apply what the recall sent.
SETTLE_SECONDS = 0.2

#: What is muted below this is open; REAPER reports a mute as 1.0 or 0.0.
_MUTED = 0.5

Opening = namedtuple("Opening", "name track target")


class Gate:
    """What REAPER last reported for the gated tracks. The feedback thread
    writes, the fade thread reads once; single dict assignments, no lock."""

    def __init__(self, tracks):
        self._names = {int(track): name for name, track in tracks.items()}
        self._muted = {}
        self._faders = {}

    def heard(self, track, word, value):
        """Take a report about a gate track's mute or fader. True if taken --
        the caller then neither relays it nor writes it to the evening."""
        if track not in self._names or word not in ("mute", "volume"):
            return False
        if word == "mute":
            self._muted[track] = float(value)
        else:
            self._faders[track] = float(value)
        return True

    def to_open(self):
        """The tracks to open, in track order, and the names of muted tracks
        that stay shut because no fader was reported for them."""
        opening, shut = [], []
        for track in sorted(self._names):
            if self._muted.get(track, 0.0) < _MUTED:
                continue
            if track not in self._faders:
                shut.append(self._names[track])
                continue
            opening.append(Opening(self._names[track], track, self._faders[track]))
        return opening, shut


def fade_frames(openings, address, steps=FADE_STEPS):
    """The messages of the fade, one list per step. Frame 0 puts each fader at
    -inf and then lifts its mute; frames 1..steps raise it, the last one to
    exactly the reported value. Built from `openings` as they are now: a
    report arriving during the fade changes nothing already planned."""
    if not openings:
        return []
    first = []
    for o in openings:
        first.append((address("track_volume", o.track), 0.0))
        first.append((address("track_mute", o.track), 0.0))
    frames = [first]
    for step in range(1, steps + 1):
        frames.append([(address("track_volume", o.track),
                        o.target if step == steps else o.target * step / steps)
                       for o in openings])
    return frames


def run_fade(frames, send, interval=FADE_SECONDS / FADE_STEPS, sleep=time.sleep):
    for index, frame in enumerate(frames):
        if index:
            sleep(interval)
        for sent_address, value in frame:
            send(sent_address, value)


def open_after_recall(gate, address, send, say, settle=SETTLE_SECONDS,
                      sleep=time.sleep):
    """Open what REAPER reported muted, after a settle. Blocking; Core runs it
    in a thread of its own so the command loop never waits on it."""
    sleep(settle)
    opening, shut = gate.to_open()
    for name in shut:
        say(f"gate: {name} stays shut, REAPER reported no fader for it")
    if not opening:
        if not shut:
            say("gate: left alone, REAPER reports its outputs open")
        return []
    run_fade(fade_frames(opening, address), send, sleep=sleep)
    say("gate: opened " + ", ".join(o.name for o in opening) + " after the recall")
    return opening


class StartupRecall:
    """The one /state/recall Core sends itself after the evening replay.

    Core's command loop is serial, so that recall is handled after every
    replayed value -- the point at which the recall has been applied. A
    device's recall carries 1 and may arrive at any time, so Core's own
    carries a random token instead (still an OSC int, as the truth says), and
    only the first one carrying it counts."""

    def __init__(self, token=None):
        self.token = token if token is not None else 2 + secrets.randbelow(2 ** 30)
        self._taken = False

    def is_it(self, osc_arguments):
        if self._taken or not osc_arguments:
            return False
        try:
            mine = int(osc_arguments[0]) == self.token
        except (TypeError, ValueError):
            return False
        if mine:
            self._taken = True
        return mine
