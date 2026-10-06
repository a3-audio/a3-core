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
        for table, width in ((render_docs.addresses_table(TRUTH), 5),
                             (render_docs.ports_table(TRUTH), 5),
                             (render_docs.routes_table(TRUTH), 3)):
            for line in table.splitlines():
                cells = [c for c in __import__("re").split(r"(?<!\\)\|", line)][1:-1]
                self.assertEqual(len(cells), width, line)

    def test_the_meters_count_from_one(self):
        table = render_docs.vu_table(TRUTH)
        self.assertIn("| `/vu/1` | in1_pre |", table)
        self.assertIn("| `/vu/40` |", table)
        self.assertIn("| `/vu/51` | in1_pre_L |", table)
        self.assertIn("| `/vu/66` | in4_post_R |", table)
        self.assertNotIn("`/vu/0`", table)

    def test_every_listener_is_a_row(self):
        table = render_docs.ports_table(TRUTH)
        for listener in TRUTH.listeners():
            self.assertIn(f"| {listener['program']} | {listener['role']} |", table)

    def test_every_route_is_a_row(self):
        table = render_docs.routes_table(TRUTH)
        for route in TRUTH.routes():
            self.assertIn(f"| {route['from']} | `{route['to']}` |", table, route)

    def test_stemdeck_sends_its_beat_packets(self):
        """As tempo master StemDeck broadcasts beats to the Pro DJ Link beat
        port (ProLinkSender::run), not only keep-alives and status."""
        table = render_docs.routes_table(TRUTH)
        self.assertIn("| stemdeck | `prolink.beat` |", table)

    def test_a_route_says_what_it_carries(self):
        """Its own mark where it has one (the meters), the listener's words
        where it has none."""
        rows = render_docs.routes_table(TRUTH).splitlines()
        self.assertIn("| stemdeck | `mixer.osc` | vu |", rows)
        self.assertIn("| stemdeck | `prolink.beat` | Pro DJ Link beat packets (broadcast, not OSC) |", rows)

    def test_a_senders_routes_stand_together(self):
        senders = [line.split("|")[1].strip()
                   for line in render_docs.routes_table(TRUTH).splitlines()[2:]]
        seen = []
        for sender in senders:
            if not seen or seen[-1] != sender:
                self.assertNotIn(sender, seen, senders)
                seen.append(sender)


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

    def test_a3_doc_is_the_umbrellas_submodule_beside_this_one(self):
        """Since 2026-10-04 ~/a3-system is the umbrella's checkout, and a3-doc
        sits beside a3-core as its submodule, not under web/."""
        self.assertEqual(render_docs.default_doc(ROOT), ROOT.parent / "a3-doc")

    def test_the_ports_page_renders_the_routes(self):
        page = render_docs.default_doc(ROOT) / "src/ressources/ports.md"
        if not page.exists():
            self.skipTest(f"no {page} -- run this in the a3-system checkout")
        text = page.read_text()
        self.assertIn("<!-- a3-osc:routes -->", text)
        self.assertIn("<!-- /a3-osc:routes -->", text)

    def test_the_committed_pages_are_the_render(self):
        doc = render_docs.default_doc(ROOT)
        if not doc.is_dir():
            self.skipTest(f"no a3-doc at {doc} -- run this in the a3-system checkout")
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
