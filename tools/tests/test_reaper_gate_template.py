"""The template's gate tracks, muted by a script on a copy (a3-system#74).

REAPER is the maintainer's: the template is never edited by hand here. The
script changes the first field of MUTESOLO on the layout's gate tracks that are
not muted yet and nothing else, and says which lines it changed, so the copy
can be diffed before it goes anywhere.
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
                                  mute_tracks, stray_changes)

PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_layout import load_layout   # noqa: E402

NAMES = ("main", "booth", "phones")

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
    def test_the_gate_names_are_the_layouts_gate_in_track_order(self):
        gate = load_layout(PACKAGE / "share/a3-core/layout.json").gate
        self.assertEqual(GATE_NAMES, tuple(sorted(gate, key=gate.get)))

    def test_exactly_the_three_mute_fields_change(self):
        changes = changed_lines(SAMPLE, mute_tracks(SAMPLE, NAMES))
        self.assertEqual([(n, new) for n, _, new in changes],
                         [(5, "    MUTESOLO 1 0 0"), (13, "    MUTESOLO 1 1 0"),
                          (17, "    MUTESOLO 1 0 0")])

    def test_rec_and_nested_blocks_are_untouched(self):
        lines = mute_tracks(SAMPLE, NAMES).splitlines()
        self.assertEqual(lines[7], "      MUTESOLO 0 0 0")
        self.assertEqual(lines[20], "    MUTESOLO 0 0 0")

    def test_twice_is_once(self):
        once = mute_tracks(SAMPLE, NAMES)
        self.assertEqual(mute_tracks(once, NAMES), once)

    def test_line_endings_survive(self):
        crlf = SAMPLE.replace("\n", "\r\n")
        self.assertEqual(mute_tracks(crlf, NAMES), mute_tracks(SAMPLE, NAMES).replace("\n", "\r\n"))

    def test_a_missing_track_is_refused(self):
        with self.assertRaises(ValueError):
            mute_tracks(SAMPLE.replace("NAME phones", "NAME headphones"), NAMES)

    def test_a_doubled_name_is_refused(self):
        with self.assertRaises(ValueError):
            mute_tracks(SAMPLE.replace("NAME rec", "NAME main"), NAMES)

    def test_an_already_muted_track_is_left_as_it_is(self):
        partly = SAMPLE.replace("MUTESOLO 0 0 0\n    <FXCHAIN",
                                "MUTESOLO 1 0 0\n    <FXCHAIN")
        changes = changed_lines(partly, mute_tracks(partly, NAMES))
        self.assertEqual([n for n, _, _ in changes], [13, 17])


class StrayChanges(unittest.TestCase):
    def test_only_gate_mute_lines_is_no_stray(self):
        self.assertEqual(stray_changes(SAMPLE, mute_tracks(SAMPLE, NAMES), NAMES), [])

    def test_a_changed_rec_line_is_a_stray(self):
        after = mute_tracks(SAMPLE, NAMES).replace(
            "NAME rec\n    MUTESOLO 0 0 0", "NAME rec\n    MUTESOLO 1 0 0")
        self.assertEqual([n for n, _, _ in stray_changes(SAMPLE, after, NAMES)], [21])

    def test_a_changed_solo_field_on_a_gate_track_is_a_stray(self):
        after = mute_tracks(SAMPLE, NAMES).replace("MUTESOLO 1 1 0", "MUTESOLO 1 0 0")
        self.assertEqual([n for n, _, _ in stray_changes(SAMPLE, after, NAMES)], [13])

    def test_a_nested_mutesolo_is_a_stray(self):
        after = mute_tracks(SAMPLE, NAMES).replace("      MUTESOLO 0 0 0",
                                                   "      MUTESOLO 1 0 0")
        self.assertEqual([n for n, _, _ in stray_changes(SAMPLE, after, NAMES)], [8])


class TheScript(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.source = Path(self.tmp.name) / "a3-reaper.RPP"
        self.source.write_text(SAMPLE)
        self.dest = Path(self.tmp.name) / "muted.RPP"
        self.run_with = lambda *paths: main([str(p) for p in paths], names=NAMES)

    def tearDown(self):
        self.tmp.cleanup()

    def test_it_writes_a_copy_and_leaves_the_source(self):
        with redirect_stdout(io.StringIO()) as out:
            self.assertEqual(self.run_with(self.source, self.dest), 0)
        self.assertEqual(self.source.read_text(), SAMPLE)
        self.assertEqual(self.dest.read_text(), mute_tracks(SAMPLE, NAMES))
        self.assertIn("3 lines changed", out.getvalue())
        self.assertIn("main, booth, phones", out.getvalue())

    def test_it_refuses_to_write_over_its_source(self):
        with redirect_stdout(io.StringIO()):
            self.assertEqual(self.run_with(self.source, self.source), 2)
        self.assertEqual(self.source.read_text(), SAMPLE)

    def test_a_partly_muted_source_changes_only_the_open_gate_tracks(self):
        partly = mute_tracks(SAMPLE, ("main", "booth"))
        self.source.write_text(partly)
        with redirect_stdout(io.StringIO()) as out:
            self.assertEqual(self.run_with(self.source, self.dest), 0)
        self.assertEqual(self.dest.read_text(), mute_tracks(SAMPLE, NAMES))
        self.assertIn("1 lines changed (phones)", out.getvalue())

    def test_a_fully_muted_source_is_a_copy_with_nothing_changed(self):
        muted = mute_tracks(SAMPLE, NAMES)
        self.source.write_text(muted)
        with redirect_stdout(io.StringIO()) as out:
            self.assertEqual(self.run_with(self.source, self.dest), 0)
        self.assertEqual(self.dest.read_text(), muted)
        self.assertIn("0 lines changed", out.getvalue())

    def test_a_stray_change_is_exit_2_and_no_copy(self):
        import reaper_gate_template as tool
        real = tool.mute_tracks
        tool.mute_tracks = lambda text, names: real(text, names).replace(
            "NAME rec\n    MUTESOLO 0", "NAME rec\n    MUTESOLO 1")
        try:
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()) as err:
                self.assertEqual(self.run_with(self.source, self.dest), 2)
        finally:
            tool.mute_tracks = real
        self.assertIn("21", err.getvalue())
        self.assertFalse(self.dest.exists())

    def test_the_shipped_template_is_accepted(self):
        template = (PACKAGE / "share/a3-core/config/REAPER/ProjectTemplates"
                    / "a3-reaper.RPP")
        with redirect_stdout(io.StringIO()) as out:
            self.assertEqual(main([str(template), str(self.dest)]), 0)
        self.assertEqual(self.dest.read_bytes()[:20], template.read_bytes()[:20])
        self.assertIn("lines changed", out.getvalue())

    def test_it_refuses_a_hard_link_to_its_source(self):
        link = Path(self.tmp.name) / "link.RPP"
        os.link(self.source, link)
        with redirect_stdout(io.StringIO()):
            self.assertEqual(self.run_with(self.source, link), 2)
        self.assertEqual(self.source.read_text(), SAMPLE)

    def test_a_missing_or_doubled_track_is_exit_2_and_no_copy(self):
        for broken in (SAMPLE.replace("NAME phones", "NAME headphones"),
                       SAMPLE.replace("NAME rec", "NAME main")):
            self.source.write_text(broken)
            with redirect_stdout(io.StringIO()), \
                    redirect_stderr(io.StringIO()) as err:
                self.assertEqual(self.run_with(self.source, self.dest), 2)
            self.assertTrue(err.getvalue())
            self.assertFalse(self.dest.exists())


if __name__ == "__main__":
    unittest.main()
