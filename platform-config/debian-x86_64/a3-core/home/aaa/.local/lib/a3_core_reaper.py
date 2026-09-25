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
