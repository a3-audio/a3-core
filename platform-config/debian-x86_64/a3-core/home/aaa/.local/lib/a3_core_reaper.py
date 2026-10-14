"""What Core asks REAPER to do, beyond setting values.

REAPER tells an OSC surface its whole state once, when REAPER starts. A Core
that started later never heard it, which is why the chain used to have to
start Core first. Action 41743, "Control surface: refresh all surfaces", makes
REAPER say everything again -- on the rig some 27,000 packets in five seconds
on the feedback port. Core asks for it once it is listening, and the window
has a key for it.

It only works while the pattern file (a3-core.ReaperOSC) keeps an ACTION
pattern; tools/tests/test_core_asks_reaper.py holds that.
"""

import time

REFRESH_ACTION = "/action/41743"

#: What ReaperProject.report says when REAPER's project changed hands.
CLOSED = "closed"
REOPENED = "reopened"


class Arrivals:
    """How many packets REAPER has sent. The feedback loop is the only writer."""

    def __init__(self):
        self.count = 0

    def tick(self):
        self.count += 1


def when_reaper_listens(heard, ask, then, every=2.0, say=print):
    """Ask REAPER to report until it answers, then run `then` once.

    Anything sent to REAPER before its OSC surface listens is lost without an
    error -- UDP has nobody to tell. On a full chain start REAPER is still
    loading plug-ins for a few seconds after Core is up, and the evening
    replay used to go out into that gap (a3-core#56). `heard` is set by the
    feedback port once REAPER reports a track the layout names -- its surface
    talks before the project has loaded, so any packet is too early. Blocking;
    run it in a thread of its own.
    """
    ask()
    waiting_said = False
    while not heard.wait(every):
        if not waiting_said:
            say(f"startup: REAPER does not answer yet; asking every {every:g} s")
            waiting_said = True
        ask()
    if waiting_said:
        say("startup: REAPER answered")
    then()


def wait_until_quiet(arrivals, then, quiet_rate=500, quiet_for=1.0,
                     poll=0.25, give_up=60.0, say=print, sleep=time.sleep):
    """Run `then` once REAPER has stopped reporting its whole state.

    A refresh is a pass over every address, rate-limited by REAPER to some
    2,300 a second, and a cold start makes two of them back to back; at idle
    only the meters are left, about 56 a second. A value set while a pass is
    running is reported with its *old* value by that pass (measured on
    2026-09-26), and Core writes what REAPER reports into evening.json -- so
    the replay has to come after the pass, never inside it. Quiet means under
    `quiet_rate` for a whole `quiet_for`: the two passes of a cold start have
    a dip between them that a single poll would take for the end.
    """
    needed = max(1, round(quiet_for / poll))
    calm = 0
    waited = 0.0
    last = arrivals.count
    while calm < needed:
        if waited >= give_up:
            say(f"startup: gave up waiting for REAPER to finish reporting "
                f"after {give_up:g} s; replaying anyway")
            break
        sleep(poll)
        waited += poll
        now = arrivals.count
        calm = calm + 1 if (now - last) / poll < quiet_rate else 0
        last = now
    then()


class ReaperProject:
    """Whether what REAPER reports is the evening.

    `reaper -closeall:nosave` -- a3-reaper.service's ExecStop, so every stop,
    restart and reboot -- makes REAPER report the project it is closing:
    every track volume as -2.75, every FX parameter as 0.0, every mute as 0
    (measured 2026-10-14). A running Core took those for the evening, wrote
    them to evening.json, and the next start replayed every fader and EQ at 0.
    The template a fresh REAPER then loads is no evening either.

    A normalised fader is never below 0, so a negative volume on a track the
    layout names is the project going away. The track's volume comes first
    in each track's block of the closing burst, so the close is seen before
    any of its zeros. From then on nothing REAPER says is the evening until
    it is back -- a volume a live track can have; the closing burst has none
    -- and Core has replayed the evening into it, the way it does at its own
    start: wait_until_quiet, the replay, the recall that opens the gate.

    Core's own start is the same story from its first line: the evening was
    read from the file and is not REAPER's word again until the replay.

    The feedback thread reports, the replay thread replays: single attribute
    assignments, no lock, like a3_core_gate.Gate.
    """

    def __init__(self, evening=()):
        self._evening = list(evening)
        #: None until REAPER has said anything: Core's own start.
        self._open = None
        self.live = False
        #: One more for every close. A replay counts only for the round it
        #: began in: one that began before a close went into a project that
        #: is gone.
        self.round = 0

    @property
    def is_open(self):
        return self._open is not False

    def report(self, word, value, current):
        """One report about a track the layout names: `word` is what it is
        about ("volume", "mute", "fxparam", ...). `current` gives the evening
        as it stands, and is asked only when a live evening is about to be
        held. Returns CLOSED, REOPENED or None."""
        if word == "volume" and value < 0:
            if self._open is False:
                return None
            if self.live:
                self._evening = list(current())
            self._open = False
            self.live = False
            self.round += 1
            return CLOSED
        if self._open is False:
            if word != "volume":
                return None
            self._open = True
            return REOPENED
        self._open = True
        return None

    def evening(self):
        """What the next replay sends: the file's evening at Core's start,
        the live evening held at the last close after that."""
        return list(self._evening)

    def replayed(self, round_started):
        """The replay that began in `round_started` is through. True if the
        evening is REAPER's word again."""
        if round_started != self.round or self._open is False:
            return False
        self.live = True
        return True


def replay_after_every_start(heard, gone, ask, replay, every=2.0, say=print,
                             rounds=None):
    """Core's start-up wait, once for Core's own start and again for every
    REAPER that closes and comes back under it. Blocking; run it in a thread
    of its own.

    `gone` is set when REAPER's project closes, after `heard` was cleared, so
    the next round asks REAPER until it answers -- exactly the wait at Core's
    own start. `rounds` is for the tests; Core never stops waiting.
    """
    done = 0
    while True:
        when_reaper_listens(heard, ask, replay, every=every, say=say)
        done += 1
        if rounds is not None and done >= rounds:
            return
        gone.wait()
        gone.clear()
