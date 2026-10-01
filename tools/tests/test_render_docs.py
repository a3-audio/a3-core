"""a3-doc's OSC and port tables are rendered from the one truth.

The OSC reference was wrong in four places on 2026-09-29, each a copy that had
not followed a change. Its tables are written by tools/render_docs.py between
markers now, and this holds a3-doc's committed pages to the render.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

import a3_osc        # noqa: E402
import render_docs   # noqa: E402

TRUTH = a3_osc.load(ROOT / "platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json")


class TheTables(unittest.TestCase):
    def test_every_address_is_a_row(self):
        table = render_docs.addresses_table(TRUTH)
        for key in TRUTH.addresses():
            self.assertIn(f"`{TRUTH.pattern(key)}`", table, key)

    def test_a_row_says_who_sends_it_and_who_hears_it(self):
        row = next(line for line in render_docs.addresses_table(TRUTH).splitlines()
                   if "`/master/aux-return`" in line)
        self.assertIn("mixer, motion", row)
        self.assertIn("core", row)

    def test_a_pipe_in_a_cell_does_not_split_the_row(self):
        # "f|s" and "/DualDelay/delayBPML|R" are cells, not column breaks.
        for table in (render_docs.addresses_table(TRUTH), render_docs.ports_table(TRUTH)):
            for line in table.splitlines():
                cells = [c for c in __import__("re").split(r"(?<!\\)\|", line)][1:-1]
                self.assertEqual(len(cells), 5, line)

    def test_the_meters_count_from_one(self):
        table = render_docs.vu_table(TRUTH)
        self.assertIn("| `/vu/1` | in1_pre |", table)
        self.assertIn("| `/vu/40` |", table)
        self.assertNotIn("`/vu/0`", table)

    def test_every_listener_is_a_row(self):
        table = render_docs.ports_table(TRUTH)
        for listener in TRUTH.listeners():
            self.assertIn(f"| {listener['program']} | {listener['role']} |", table)


class TheMarkers(unittest.TestCase):
    def test_only_between_the_markers_is_replaced(self):
        page = ("# OSC\n\nHand text.\n\n<!-- a3-osc:vu -->\nold\n"
                "<!-- /a3-osc:vu -->\n\nMore.\n")
        out = render_docs.put_tables(page, TRUTH)
        self.assertIn("Hand text.", out)
        self.assertIn("More.", out)
        self.assertNotIn("\nold\n", out)
        self.assertIn("`/vu/1`", out)

    def test_a_page_without_markers_is_left_as_it_is(self):
        page = "# Nothing to render here\n"
        self.assertEqual(render_docs.put_tables(page, TRUTH), page)

    def test_a_second_render_changes_nothing(self):
        page = "<!-- a3-osc:ports -->\n<!-- /a3-osc:ports -->\n"
        once = render_docs.put_tables(page, TRUTH)
        self.assertEqual(render_docs.put_tables(once, TRUTH), once)


class A3DocFollows(unittest.TestCase):
    """a3-doc beside this checkout: its pages hold what the truth renders."""

    def test_the_committed_pages_are_the_render(self):
        doc = ROOT.parent / "web" / "a3-doc"
        if not doc.is_dir():
            self.skipTest(f"no a3-doc at {doc} -- run this in the a3-system workspace")
        for page in render_docs.PAGES:
            path = doc / page
            if not path.exists():
                continue
            text = path.read_text()
            self.assertEqual(render_docs.put_tables(text, TRUTH), text,
                             f"{page} is not what a3-osc.json renders -- "
                             f"run tools/render_docs.py")


if __name__ == "__main__":
    unittest.main()
