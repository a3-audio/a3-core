"""Core carries stems on the desk: words mapped, state kept, recall says it."""

import ast
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

from a3_core_stems import Stems                      # noqa: E402
from a3_core_state import apply_stems, state_of      # noqa: E402

CORE = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()


class TheStateFile(unittest.TestCase):
    def test_stems_are_written_down(self):
        s = Stems()
        s.turn_channel(0, +1)
        self.assertEqual(state_of([], _Master(), s)["stems"], s.as_data())

    def test_a_state_without_stems_is_all_none(self):
        self.assertEqual(apply_stems({}).channel_pair, [0, 0, 0, 0])
        self.assertEqual(apply_stems({"stems": "garbage"}).channel_pair, [0, 0, 0, 0])

    def test_stems_survive_the_round_trip(self):
        s = Stems()
        s.turn_channel(2, +3)
        again = apply_stems(state_of([], _Master(), s))
        self.assertEqual(again.channel_pair, s.channel_pair)


class _Master:
    class _Mode:
        value = "low_pass"
    fx_mode = _Mode()


class CoreListens(unittest.TestCase):
    def test_core_still_parses(self):
        ast.parse(CORE)

    def test_the_return_family_is_mapped(self):
        self.assertIn('("fx-return", osc_handler_fx_return)', CORE)

    def test_the_channel_turn_has_its_branch(self):
        self.assertIn('elif parameter == "stem.turn":', CORE)

    def test_recall_and_start_speak_the_stems(self):
        self.assertGreaterEqual(CORE.count("speak_stems("), 3)   # def, recall, start-up


if __name__ == "__main__":
    unittest.main()
