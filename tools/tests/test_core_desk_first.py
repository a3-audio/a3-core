"""The desk beats Core's own evening replay (a3_core_desk_first)."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]
                       / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_desk_first import DeskFirst  # noqa: E402

GAIN = "/channel/1/gain"
EQ = "/channel/1/eq/low"
VOLUME = "/master/volume"
EVENING = [(GAIN, 0.2), (EQ, 0.3)]


class WithoutTheDesk(unittest.TestCase):
    def test_the_evening_is_replayed_as_it_was(self):
        self.assertEqual(DeskFirst().over(EVENING), EVENING)

    def test_a_motion_touch_alone_does_not_change_the_replay(self):
        """Motion keeps today's rule: no claim of its own on the replay."""
        desk_first = DeskFirst()
        desk_first.heard(GAIN, 0.9, from_desk=False)
        self.assertEqual(desk_first.over(EVENING), EVENING)


class TheDeskSentFirst(unittest.TestCase):
    """The desk's dump before the replay: the replay carries the desk's
    value, not the evening's -- what the desk sent before REAPER listened
    was lost on the way, so skipping the address would leave the template."""

    def test_the_desks_value_replaces_the_evenings(self):
        desk_first = DeskFirst()
        desk_first.heard(GAIN, 0.8, from_desk=True)
        self.assertEqual(dict(desk_first.over(EVENING)),
                         {GAIN: 0.8, EQ: 0.3})

    def test_the_desks_latest_value_counts(self):
        desk_first = DeskFirst()
        desk_first.heard(GAIN, 0.8, from_desk=True)
        desk_first.heard(GAIN, 0.6, from_desk=True)
        self.assertEqual(dict(desk_first.over(EVENING))[GAIN], 0.6)

    def test_an_address_the_evening_lacks_is_replayed_too(self):
        desk_first = DeskFirst()
        desk_first.heard(VOLUME, 0.7, from_desk=True)
        self.assertEqual(dict(desk_first.over(EVENING))[VOLUME], 0.7)

    def test_a_later_touch_elsewhere_on_a_desk_address_wins(self):
        """Latest touch wins, as between the desk and Motion today."""
        desk_first = DeskFirst()
        desk_first.heard(GAIN, 0.8, from_desk=True)
        desk_first.heard(GAIN, 0.4, from_desk=False)
        self.assertEqual(dict(desk_first.over(EVENING))[GAIN], 0.4)

    def test_it_holds_for_every_replay_of_the_run(self):
        """A REAPER restart replays again; the desk is still in front."""
        desk_first = DeskFirst()
        desk_first.heard(GAIN, 0.8, from_desk=True)
        desk_first.over(EVENING)
        self.assertEqual(dict(desk_first.over(EVENING))[GAIN], 0.8)


class TheDeskSentAfter(unittest.TestCase):
    """The desk's dump after the replay is ordinary traffic: it reaches
    REAPER and the evening the way any touch does, and the next replay
    (a REAPER restart) carries it."""

    def test_a_replay_after_a_late_dump_carries_the_desk(self):
        desk_first = DeskFirst()
        self.assertEqual(desk_first.over(EVENING), EVENING)
        desk_first.heard(GAIN, 0.8, from_desk=True)
        self.assertEqual(dict(desk_first.over(EVENING))[GAIN], 0.8)


if __name__ == "__main__":
    unittest.main()
