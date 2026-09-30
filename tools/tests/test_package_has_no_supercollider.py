"""SuperCollider is obsolete (maintainer, 2026-09-30).

It made the VU meters until the beat-analyzer took them over, and then sat in
the package for weeks: three scripts, a unit nothing started, and two buttons
in the old interface that started a unit by a name that no longer existed
(`a3_vu_meter`). A package that ships a program nobody runs is a package whose
documentation keeps naming it -- five pages of a3-doc still said the VU came
from "the SuperCollider backend".
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"


class ThePackageHasNoSuperCollider(unittest.TestCase):
    def test_no_script(self):
        self.assertEqual(sorted(PACKAGE.rglob("*.scd")), [])

    def test_no_unit(self):
        self.assertEqual(sorted(PACKAGE.rglob("a3-supercollider.service")), [])

    def test_nothing_starts_it(self):
        for path in PACKAGE.rglob("*"):
            if not path.is_file() or path.suffix in (".png", ".jpg", ".RPP"):
                continue
            text = path.read_text(errors="replace").lower()
            self.assertNotIn("sclang", text, path)
            self.assertNotIn("start supercollider", text, path)


if __name__ == "__main__":
    unittest.main()
