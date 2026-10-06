"""Core points zita-j2n at the active StemDeck (decided 2026-10-06).

Read off the source and the unit, like test_core_motion_wiring: Core itself
cannot be started in a test. The decisions are tested in test_core_return;
this holds that a3-core.py asks them and that the unit reads the answer.
"""

import ast
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCAL = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
CORE = (LOCAL / "bin/a3-core.py").read_text()
UNIT = LOCAL / "share/a3-core/config/systemd/user/zita-j2n.service"


def _source_of(function_name):
    node = next(n for n in ast.walk(ast.parse(CORE))
                if isinstance(n, ast.FunctionDef) and n.name == function_name)
    return ast.get_source_segment(CORE, node)


class TheUnit(unittest.TestCase):
    def environment_files(self):
        return [line.split("=", 1)[1] for line in UNIT.read_text().splitlines()
                if line.startswith("EnvironmentFile=")]

    def test_reads_the_return_file_after_osc_env(self):
        """systemd applies later EnvironmentFile= lines last, so the return
        file's two values win over osc.env's."""
        self.assertEqual(self.environment_files(),
                         ["%h/.config/a3/osc.env",
                          "-%h/.config/a3/zita-return.env"])

    def test_a_missing_return_file_is_fine(self):
        self.assertTrue(self.environment_files()[-1].startswith("-"))


class TheStemDeckMovesTheReturn(unittest.TestCase):
    def test_its_hello(self):
        self.assertIn("follow_the_return(", _source_of("stemdeck_said_hello"))

    def test_its_silence(self):
        self.assertIn("follow_the_return(None)",
                      _source_of("notice_stemdeck_silence"))

    def test_only_a_change_reaches_zita(self):
        source = _source_of("follow_the_return")
        self.assertIn("_return.follow(", source)
        self.assertIn("is None", source)
        self.assertIn("point_zita_at(", source)


class AtStartUp(unittest.TestCase):
    def test_core_resets_the_return_to_radla(self):
        self.assertIn("start_at_radla(", CORE)
        self.assertIn('_truth.endpoint("radla", "zita-n2j")', CORE)


if __name__ == "__main__":
    unittest.main()
