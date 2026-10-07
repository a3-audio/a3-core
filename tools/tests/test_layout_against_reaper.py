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
}

MASTER_NAME = {
    "track_masterbus": "dec_master",
    "track_booth": "dec_booth",
    "track_phones": "dec_phones",
    "aux_return": "aux_return",
    "track_stems": "stems",
    "track_analog": "analog",
}

#: The project the layout ships beside. Installed as the template REAPER starts
#: from (a3-reaper.service passes it with -template).
SHIPPED_PROJECT = (PACKAGE / "share/a3-core/config/REAPER/ProjectTemplates"
                   / "a3-reaper.RPP")
SHIPPED_OSC_PATTERN = PACKAGE / "share/a3-core/config/REAPER/OSC/a3-core.ReaperOSC"


def osc_track_bank_size(path):
    """DEVICE_TRACK_COUNT: REAPER answers /track/N only for N up to this."""
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"DEVICE_TRACK_COUNT\s+(\d+)", line)
        if match:
            return int(match.group(1))
    return 0


def track_names_in_project(path):
    """REAPER's track names, one-based, read straight out of an .RPP file.

    A track's own NAME sits one level inside its <TRACK block, at four spaces;
    anything deeper belongs to an envelope or a plug-in.
    """
    names = {}
    track = 0
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("  <TRACK"):
            track += 1
        elif track and line.startswith("    NAME "):
            names[track] = line[len("    NAME "):].strip().strip('"')
    return names


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
        # Three tracks in the project belong to no layout field. Named here
        # so a *fourth* one appearing is a question rather than a shrug.
        # enc_fx joined enc_main and enc_phones on 2026-09-29, when the FX
        # return moved to its own track: it is still reached, as send "fx" of
        # every channelbus, but no longer by a track number.
        claimed = {getattr(self.layout.channel(i), f)
                   for i in range(self.layout.channel_count)
                   for f in CHANNEL_SUFFIX}
        claimed |= {getattr(self.layout.master, f) for f in MASTER_FIELDS}

        unclaimed = {t: n for t, n in self.names.items() if t not in claimed}
        # ENCODER, DECODER: folder tracks (template of 2026-10-01). analog left
        # this list for track_analog (spec stemdeck-remote): Core mutes its
        # sends to the channel inputs while a stem plays there.
        self.assertEqual(sorted(unclaimed.values()),
                         ["DECODER", "ENCODER", "INPUT", "enc_fx", "enc_main", "enc_phones"], unclaimed)

    def test_the_recorded_names_are_the_shipped_projects(self):
        # The recording is only as good as the project it was taken from. A
        # track inserted in the template shifts every number after it -- the
        # Return track did that on 2026-09-29 -- and a recording left behind
        # would keep this whole file green against a project that is gone.
        shipped = track_names_in_project(SHIPPED_PROJECT)
        for track, name in self.names.items():
            with self.subTest(track=track):
                self.assertEqual(shipped.get(track), name,
                                 f"track {track} is {name!r} in the recording "
                                 f"and {shipped.get(track)!r} in the template")


if __name__ == "__main__":
    unittest.main()


class OscReachesEveryTrack(unittest.TestCase):
    """REAPER drops /track/N silently when N is beyond its OSC track bank.

    The bank was 27 while the project had 27 tracks; the Return track made
    it 28 and the FX-return pot drove nothing, with Core sending faithfully.
    """

    def test_the_osc_track_bank_covers_every_track_in_the_shipped_project(self):
        tracks = len(track_names_in_project(SHIPPED_PROJECT))
        self.assertGreaterEqual(osc_track_bank_size(SHIPPED_OSC_PATTERN), tracks)


def send_destinations_in_project(path):
    """Per sending track (one-based), the tracks its sends reach, in send
    order. REAPER stores a send as an AUXRECV line in the *receiving* track,
    with the sender zero-based; a track's send N is its Nth such line in
    project order."""
    destinations = {}
    track = 0
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("  <TRACK"):
            track += 1
        match = re.match(r"\s+AUXRECV (\d+) ", line)
        if track and match:
            destinations.setdefault(int(match.group(1)) + 1, []).append(track)
    return destinations


class TheReturnsReceivesAreTheTemplates(unittest.TestCase):
    """The return mode opens one of aux_return's two receives by send volume
    (return-sources, 2026-10-07). A send number that shifts in the template
    would open a channel input instead, so it is read from the project."""

    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")
        self.sends = send_destinations_in_project(SHIPPED_PROJECT)

    def _reaches(self, track, send):
        return self.sends[track][send - 1]

    def test_the_analog_send_reaches_the_return(self):
        master = self.layout.master
        self.assertEqual(self._reaches(master.track_analog, self.layout.send("analog_to_return")),
                         master.aux_return)

    def test_the_stems_send_reaches_the_return(self):
        master = self.layout.master
        self.assertEqual(self._reaches(master.track_stems, self.layout.send("stems_to_return")),
                         master.aux_return)

    def test_the_channel_analog_sends_reach_the_inputs(self):
        # The same reading, held against the numbers measured by ear in 2026-10-01.
        for index in range(self.layout.channel_count):
            channel = self.layout.channel(index)
            self.assertEqual(self._reaches(self.layout.master.track_analog, channel.analog_send),
                             channel.track_input)


class TheGateIsTheTemplatesOutputs(unittest.TestCase):
    """The gated tracks are the template's speaker feeds and the main meter's
    track, read by name from the shipped project (the recording stops at
    track 28)."""

    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")
        self.names = track_names_in_project(SHIPPED_PROJECT)

    def test_each_gate_track_has_its_name_in_the_template(self):
        for name, track in self.layout.gate.items():
            with self.subTest(name=name):
                self.assertEqual(self.names.get(track), name)

    def test_rec_is_never_gated(self):
        # rec feeds zita-j2n, StemDeck's rec input and the analyzer's bpm input.
        rec = next(track for track, name in self.names.items() if name == "rec")
        self.assertNotIn(rec, self.layout.gate.values())

    def test_the_main_meter_is_gated(self):
        # main_vu feeds out41-50, which beat-analyzer turns into the desk's
        # main meter; gated, the meter is dark while the room is silent.
        self.assertEqual(self.layout.gate.get("main_vu"), 38)
        self.assertEqual(self.names.get(38), "main_vu")

    def test_reaper_reports_every_gate_track(self):
        self.assertLessEqual(max(self.layout.gate.values()),
                             osc_track_bank_size(SHIPPED_OSC_PATTERN))
