"""Where a value REAPER reports goes, now that two devices have the knob.

Until 2026-09-12 every entry in CHANNEL_REVERSALS went to `mixer` alone, and
the comment above the table said why: *"these are the mixer's channel strip,
and telling Motion about them would be telling it about controls it does not
have."* That was true when it was written. A3 Motion grew a software channel
strip on 2026-09-10, and the sentence quietly stopped being true a quarter of
a year before anybody looked -- which is how the maintainer came to find GAIN
and VOL sitting at zero on a rig that was making sound.

So one value now becomes one message per device, and this is the shape of
that fan-out. Kept out of a3-core.py on purpose: no test imports that file --
importing it opens sockets and starts a server -- and "suite green, program
broken" has been the failure mode of this week three times over.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_layout import load_layout                     # noqa: E402
from a3_core_reverse import (CHANNEL_REVERSALS, Reverse,   # noqa: E402
                             reversed_messages)


class TheFanOut(unittest.TestCase):
    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    def messages(self, entry, channel=1, value=0.25):
        return list(reversed_messages(self.layout, entry, channel, value))

    def test_one_value_becomes_one_message_per_device(self):
        entry = Reverse("track_input", "gain", 1, "slope_volume", "gain",
                        ("mixer", "motion"))
        self.assertEqual(
            self.messages(entry),
            [("mixer", "/channel/1/gain", 0.25),
             ("motion", "/channel/1/gain", 0.25)])

    def test_the_address_is_the_same_one_for_every_device(self):
        """Both ends set the control on the same address, so both are told on
        it. A per-device spelling would be a second protocol to keep in step,
        and the drift would show as one device going quiet."""
        entry = Reverse("track_input", "eq", 1, "slope_eq", "eq/high",
                        ("mixer", "motion"))
        addresses = {address for _, address, _ in self.messages(entry)}
        self.assertEqual(addresses, {"/channel/1/eq/high"})

    def test_one_device_still_means_one_message(self):
        """`to` is a tuple even with one name in it. A bare string would
        iterate into six single-letter device names and send to none of
        them -- the kind of bug that sends nothing and says nothing."""
        entry = Reverse("track_input", "gain", 1, "slope_volume", "gain",
                        ("mixer",))
        self.assertEqual(len(self.messages(entry)), 1)

    def test_the_order_is_the_order_written_down(self):
        """So the same fader move replays the same way twice, the way
        a3_core_recall.Relayed keeps its own order."""
        entry = Reverse("track_input", "gain", 1, "slope_volume", "gain",
                        ("motion", "mixer"))
        self.assertEqual([device for device, _, _ in self.messages(entry)],
                         ["motion", "mixer"])


class EveryReversalReachesBothDevices(unittest.TestCase):
    """The decision of 2026-09-12: *an beide, fortlaufend*.

    Both, because the desk is still the desk and taking its feed away would be
    a second decision nobody asked for. Continuously rather than on request,
    because none of these five carries an accent envelope -- REAPER's value is
    Motion's value, so there is nothing for a relay to ratchet. That is what
    separates them from the two encoder pots, which had to come out the same
    day: see issues/a3-core-dauernder-rueckweg-ist-eine-schleife.md.
    """

    def test_all_five_go_to_both(self):
        for entry in CHANNEL_REVERSALS:
            with self.subTest(control=entry.address):
                self.assertEqual(tuple(entry.to), ("mixer", "motion"))

    def test_to_is_never_a_bare_string(self):
        for entry in CHANNEL_REVERSALS:
            with self.subTest(control=entry.address):
                self.assertNotIsInstance(entry.to, str)

    def test_no_device_is_named_twice(self):
        """Two entries for one device would send the same value twice and
        note it twice, which a recall would then replay twice."""
        for entry in CHANNEL_REVERSALS:
            with self.subTest(control=entry.address):
                self.assertEqual(len(set(entry.to)), len(entry.to))

    def test_every_device_named_is_one_Core_has_a_client_for(self):
        """`client_for` in a3-core.py answers with the mixer's client for
        "mixer" and Motion's for everything else, so a typo here would send
        a channel's gain to Motion under a name that reads as neither."""
        for entry in CHANNEL_REVERSALS:
            for device in entry.to:
                with self.subTest(control=entry.address, device=device):
                    self.assertIn(device, ("mixer", "motion"))


if __name__ == "__main__":
    unittest.main()
