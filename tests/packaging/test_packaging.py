# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pins the Debian packaging that a3-system's installer/package.py reads:
packaging/stage lays out the bench binary, packaging/DEBIAN/control names it."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STAGE = ROOT / "packaging" / "stage"
CONTROL = ROOT / "packaging" / "DEBIAN" / "control"


def control_fields():
    fields = {}
    for line in CONTROL.read_text().splitlines():
        if line and not line.startswith(" ") and ":" in line:
            key, value = line.split(":", 1)
            fields[key] = value.strip()
    return fields


class StageTest(unittest.TestCase):
    def test_files_installs_the_bench_executable(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp, "build")
            (build / "bench").mkdir(parents=True)
            (build / "bench" / "a3-engine-bench").write_text("#!/bin/sh\n")
            stage = Path(tmp, "stage")
            subprocess.run([str(STAGE), "files", str(ROOT), str(build), str(stage)], check=True)
            installed = stage / "usr" / "bin" / "a3-engine-bench"
            self.assertTrue(installed.is_file())
            self.assertTrue(os.access(installed, os.X_OK))

    def test_the_shipped_copyright_carries_the_mit_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp, "build")
            (build / "bench").mkdir(parents=True)
            (build / "bench" / "a3-engine-bench").write_text("#!/bin/sh\n")
            stage = Path(tmp, "stage")
            subprocess.run([str(STAGE), "files", str(ROOT), str(build), str(stage)], check=True)
            copyright = (stage / "usr/share/doc/a3-engine-bench/copyright").read_text()
        self.assertIn("\nLicense: MIT\n", copyright)
        self.assertIn("Copyright (c) 2018 Chris Johnson", copyright)
        # the licence body, line for line (blank lines are " ." in this format)
        for line in (ROOT / "LICENSES" / "MIT.txt").read_text().splitlines():
            self.assertIn(" " + (line or "."), copyright.splitlines())

    def test_unknown_mode_is_refused(self):
        result = subprocess.run([str(STAGE), "nonsense"], capture_output=True)
        self.assertEqual(result.returncode, 2)


class ControlTest(unittest.TestCase):
    def test_names_the_package_and_takes_libraries_from_shlibs(self):
        fields = control_fields()
        self.assertEqual(fields["Package"], "a3-engine-bench")
        self.assertIn("${shlibs:Depends}", fields["Depends"])
        self.assertEqual(fields["Architecture"], "amd64")

    def test_maintainer_scripts_exist_and_are_executable(self):
        # installer/package.py refuses a package without all three.
        for name in ("postinst", "prerm", "postrm"):
            script = CONTROL.parent / name
            self.assertTrue(os.access(script, os.X_OK), name)

    def test_the_truth_file_comes_with_a_recommended_a3_core(self):
        self.assertIn("a3-core", control_fields().get("Recommends", ""))


if __name__ == "__main__":
    unittest.main()
