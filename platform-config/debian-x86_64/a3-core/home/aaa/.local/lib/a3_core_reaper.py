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

REFRESH_ACTION = "/action/41743"


def when_reaper_listens(heard, ask, then, every=2.0, say=print):
    """Ask REAPER to report until it answers, then run `then` once.

    Anything sent to REAPER before its OSC surface listens is lost without an
    error -- UDP has nobody to tell. On a full chain start REAPER is still
    loading plug-ins for a few seconds after Core is up, and the evening
    replay used to go out into that gap (a3-core#56). `heard` is set by the
    feedback port on the first packet from REAPER, whether it answers `ask`
    or announces itself; blocking, so run it in a thread of its own.
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
