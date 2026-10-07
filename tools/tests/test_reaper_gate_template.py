"""The template's gate tracks, muted by a script on a copy (a3-system#74).

REAPER is the maintainer's: the template is never edited by hand here. The
script changes the first field of MUTESOLO on the tracks named main, booth and
phones and nothing else, and says which lines it changed, so the copy can be
diffed before it goes anywhere.
"""

import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from reaper_gate_template import (GATE_NAMES, changed_lines, main,   # noqa: E402
                                  mute_tracks)

SAMPLE = """<REAPER_PROJECT 0.1
  <TRACK {A}
    NAME main
    VOLPAN 0.02857590543375 0 -1 -1 1
    MUTESOLO 0 0 0
    <FXCHAIN
      NAME "not a track"
      MUTESOLO 0 0 0
    >
  >
  <TRACK {B}
    NAME booth
    MUTESOLO 0 1 0
  >
  <TRACK {C}
    NAME phones
    MUTESOLO 0 0 0
  >
  <TRACK {D}
    NAME rec
    MUTESOLO 0 0 0
  >
>
"""


class MuteTracks(unittest.TestCase):
    def test_the_gate_names_are_the_three_outputs(self):
        self.assertEqual(GATE_NAMES, ("main", "booth", "phones"))

    def test_exactly_the_three_mute_fields_change(self):
        changes = changed_lines(SAMPLE, mute_tracks(SAMPLE))
        self.assertEqual([(n, new) for n, _, new in changes],
                         [(5, "    MUTESOLO 1 0 0"), (13, "    MUTESOLO 1 1 0"),
                          (17, "    MUTESOLO 1 0 0")])

    def test_rec_and_nested_blocks_are_untouched(self):
        lines = mute_tracks(SAMPLE).splitlines()
        self.assertEqual(lines[7], "      MUTESOLO 0 0 0")
        self.assertEqual(lines[20], "    MUTESOLO 0 0 0")

    def test_twice_is_once(self):
        once = mute_tracks(SAMPLE)
        self.assertEqual(mute_tracks(once), once)

    def test_line_endings_survive(self):
        crlf = SAMPLE.replace("\n", "\r\n")
        self.assertEqual(mute_tracks(crlf), mute_tracks(SAMPLE).replace("\n", "\r\n"))

    def test_a_missing_track_is_refused(self):
        with self.assertRaises(ValueError):
            mute_tracks(SAMPLE.replace("NAME phones", "NAME headphones"))

    def test_a_doubled_name_is_refused(self):
        with self.assertRaises(ValueError):
            mute_tracks(SAMPLE.replace("NAME rec", "NAME main"))


class TheScript(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.source = Path(self.tmp.name) / "a3-reaper.RPP"
        self.source.write_text(SAMPLE)
        self.dest = Path(self.tmp.name) / "muted.RPP"

    def tearDown(self):
        self.tmp.cleanup()

    def test_it_writes_a_copy_and_leaves_the_source(self):
        with redirect_stdout(io.StringIO()) as out:
            self.assertEqual(main([str(self.source), str(self.dest)]), 0)
        self.assertEqual(self.source.read_text(), SAMPLE)
        self.assertEqual(self.dest.read_text(), mute_tracks(SAMPLE))
        self.assertIn("3 lines changed", out.getvalue())

    def test_it_refuses_to_write_over_its_source(self):
        with redirect_stdout(io.StringIO()):
            self.assertEqual(main([str(self.source), str(self.source)]), 2)
        self.assertEqual(self.source.read_text(), SAMPLE)

    def test_an_already_muted_source_is_not_three_changes(self):
        self.source.write_text(mute_tracks(SAMPLE))
        with redirect_stdout(io.StringIO()):
            self.assertEqual(main([str(self.source), str(self.dest)]), 1)
        self.assertFalse(self.dest.exists())

    def test_it_refuses_a_hard_link_to_its_source(self):
        link = Path(self.tmp.name) / "link.RPP"
        os.link(self.source, link)
        with redirect_stdout(io.StringIO()):
            self.assertEqual(main([str(self.source), str(link)]), 2)
        self.assertEqual(self.source.read_text(), SAMPLE)

    def test_a_missing_or_doubled_track_is_exit_2_and_no_copy(self):
        for broken in (SAMPLE.replace("NAME phones", "NAME headphones"),
                       SAMPLE.replace("NAME rec", "NAME main")):
            self.source.write_text(broken)
            with redirect_stdout(io.StringIO()), \
                    redirect_stderr(io.StringIO()) as err:
                self.assertEqual(main([str(self.source), str(self.dest)]), 2)
            self.assertTrue(err.getvalue())
            self.assertFalse(self.dest.exists())


if __name__ == "__main__":
    unittest.main()
