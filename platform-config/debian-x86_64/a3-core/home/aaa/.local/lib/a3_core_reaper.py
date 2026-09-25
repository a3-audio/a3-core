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
