"""a3-doc's clip table is rendered from the clips Motion ships.

The library page's clip table was hand-written from the library plan, with one
character phrase a phase, while the clip files said nothing about themselves.
Each clip carries its own mood now (a3-motion-ui feat/clip-mood), and
tools/render_library.py writes the table from the files between markers, so
the page can't drift from what a device actually plays.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import render_library  # noqa: E402


def clip(name, svg="Orbit Circle", mood="a mood"):
    return {"name": name, "svg": svg, "mood": mood}


def a_set(name, *clips):
    return {"name": name, "channels": [{"slots": [{"clip": c}]} for c in clips]}


class TheTable(unittest.TestCase):
    def test_a_clip_is_a_row_with_its_shape_and_mood(self):
        table = render_library.clips_table(
            [clip("Warmup Halo", "Orbit Circle", "a slow halo overhead")],
            [a_set("Warmup", "Warmup Halo")])
        self.assertIn("| **Warmup** | Halo | Orbit Circle | a slow halo overhead |", table)

    def test_the_phase_is_named_once_for_its_rows(self):
        table = render_library.clips_table(
            [clip("Warmup Halo"), clip("Warmup Breath")],
            [a_set("Warmup", "Warmup Halo", "Warmup Breath")])
        self.assertEqual(table.count("**Warmup**"), 1)

    def test_a_clip_its_phase_set_leaves_out_is_the_spare(self):
        table = render_library.clips_table(
            [clip("Warmup Halo"), clip("Warmup Drift")],
            [a_set("Warmup", "Warmup Halo")])
        self.assertIn("| Drift (spare) |", table)
        self.assertIn("| Halo |", table)

    def test_a_phase_lists_its_set_by_channel_then_the_spare(self):
        table = render_library.clips_table(
            [clip("Warmup Drift"), clip("Warmup Sunrise"), clip("Warmup Halo")],
            [a_set("Warmup", "Warmup Sunrise", "Warmup Halo")])
        self.assertLess(table.index("Sunrise"), table.index("Halo"))
        self.assertLess(table.index("Halo"), table.index("Drift"))

    def test_phases_come_in_the_order_of_a_night(self):
        table = render_library.clips_table(
            [clip("Closing Still"), clip("Warmup Halo")], [])
        self.assertLess(table.index("Warmup"), table.index("Closing"))

    def test_a_clip_without_a_shape_is_not_a_row(self):
        # Default is the fallback for a channel without a clip, not a clip of a phase.
        table = render_library.clips_table([{"name": "Default"}], [])
        self.assertNotIn("Default", table)

    def test_a_phase_the_renderer_does_not_know_is_refused(self):
        # A new set (Space, say) must be placed in the night's order on purpose,
        # not slip to the end of the table unseen.
        with self.assertRaises(ValueError):
            render_library.clips_table([clip("Space Anchor")], [])

    def test_a_clip_without_a_mood_shows_a_dash(self):
        table = render_library.clips_table([clip("Warmup Halo", mood="")], [])
        self.assertIn("| Orbit Circle | – |", table)

    def test_a_page_without_markers_is_left_as_it_is(self):
        page = "# Nothing to render here\n"
        self.assertEqual(render_library.put_table(page, [], []), page)

    def test_a_second_render_changes_nothing(self):
        page = "<!-- a3-motion:clips -->\n<!-- /a3-motion:clips -->\n"
        once = render_library.put_table(page, [clip("Warmup Halo")], [])
        self.assertEqual(render_library.put_table(once, [clip("Warmup Halo")], []), once)


class A3DocFollows(unittest.TestCase):
    """a3-doc and Motion beside this checkout: the page holds what the clips render."""

    def test_motion_and_a3_doc_sit_beside_this_checkout(self):
        self.assertEqual(render_library.default_motion(ROOT), ROOT.parent / "a3-motion" / "ui")
        self.assertEqual(render_library.default_doc(ROOT), ROOT.parent / "a3-doc")

    def test_the_committed_page_is_the_render(self):
        motion = render_library.default_motion(ROOT)
        page = render_library.default_doc(ROOT) / render_library.PAGE
        if not (motion / render_library.CLIPS).is_dir() or not page.exists():
            self.skipTest("no Motion or a3-doc beside this checkout -- run this in the a3-system checkout")
        clips, sets = render_library.load(motion)
        if not any(c.get("mood") for c in clips):
            self.skipTest("the pinned Motion has no clip moods yet (a3-motion-ui feat/clip-mood)")
        text = page.read_text()
        self.assertIn("<!-- a3-motion:clips -->", text, "the page lost its markers")
        self.assertEqual(render_library.put_table(text, clips, sets), text,
                         f"{render_library.PAGE} is not what the clips render -- "
                         f"run tools/render_library.py")


if __name__ == "__main__":
    unittest.main()
