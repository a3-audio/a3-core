"""The layout, held against REAPER's own track names.

**The test that was written off as impossible.** test_shipped_layout.py says
it: "The layout has to agree with the REAPER project it ships beside, and
nothing here reads .RPP files. A wrong-but-well-formed track number is caught
by a person hearing the wrong channel move."

REAPER simply says. Point its OSC output at tools/listen-to-reaper.py and it
sends every track name it has -- twenty-seven of them, recorded in
tools/reaper-track-names.json.

So a wrong number is now caught here rather than by an ear, and it is caught
by name: track 12 is called "1-input", which is channel 0's input in the
one-based naming the project uses.
"""

import json
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"

sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_layout import MASTER_FIELDS, load_layout   # noqa: E402

#: What a channel's track is called in the project, per field. REAPER names
#: them one-based -- "1-input" is channel 0 here -- which is the one place
#: that difference has to be written down.
CHANNEL_SUFFIX = {
    "track_input": "input",
    "track_multi_enc": "multi-enc",
    "track_stereo_enc": "stereo-enc",
    "track_channelbus": "channelbus",
    "track_pfl": "pfl",
}

MASTER_NAME = {
    "track_masterbus": "dec_master",
    "track_booth": "dec_booth",
    "track_phones": "dec_phones",
    "track_ph_mix": "ph-mix",
    "aux_return": "enc_fx",
}


class LayoutAgainstReaper(unittest.TestCase):
    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")
        recorded = json.loads((ROOT / "tools/reaper-track-names.json").read_text())
        self.names = {int(k): v for k, v in recorded["track_names"].items()}

    def test_every_channel_track_is_the_track_reaper_calls_it(self):
        for index in range(self.layout.channel_count):
            channel = self.layout.channel(index)
            for field, suffix in CHANNEL_SUFFIX.items():
                track = getattr(channel, field)
                self.assertIn(track, self.names,
                              f"channel {index}.{field} is track {track}, "
                              f"which REAPER does not have")
                self.assertEqual(
                    self.names[track], f"{index + 1}-{suffix}",
                    f"channel {index}.{field} points at track {track}, "
                    f"which REAPER calls {self.names[track]!r}")

    def test_every_master_track_is_the_track_reaper_calls_it(self):
        for field in MASTER_FIELDS:
            track = getattr(self.layout.master, field)
            self.assertIn(track, self.names, f"master.{field} is track {track}")
            self.assertEqual(self.names[track], MASTER_NAME[field],
                             f"master.{field} points at track {track}, "
                             f"which REAPER calls {self.names[track]!r}")

    def test_the_tracks_a3_does_not_name_are_known_and_deliberate(self):
        # Two tracks in the project belong to nothing A3 addresses. Named here
        # so a *third* one appearing is a question rather than a shrug.
        claimed = {getattr(self.layout.channel(i), f)
                   for i in range(self.layout.channel_count)
                   for f in CHANNEL_SUFFIX}
        claimed |= {getattr(self.layout.master, f) for f in MASTER_FIELDS}

        unclaimed = {t: n for t, n in self.names.items() if t not in claimed}
        self.assertEqual(sorted(unclaimed.values()),
                         ["enc_main", "enc_phones"], unclaimed)


if __name__ == "__main__":
    unittest.main()
