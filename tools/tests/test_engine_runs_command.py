# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""a3-engine-runs: the one start-time rule, as an exit status (plan 4, D22)."""

import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/bin"
        / "a3-engine-runs")


def run(args, engine_script=None):
    env = dict(os.environ)
    env.pop("A3_ENGINE_BINARY", None)
    with tempfile.TemporaryDirectory() as tmp:
        if engine_script is not None:
            fake = Path(tmp) / "a3-engine"
            fake.write_text(engine_script)
            fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
            env["A3_ENGINE_BINARY"] = str(fake)
        else:
            env["A3_ENGINE_BINARY"] = str(Path(tmp) / "missing")
        return subprocess.run([sys.executable, str(TOOL), *args],
                              capture_output=True, text=True, env=env)


GOOD = "#!/bin/sh\nexit 0\n"
BAD = "#!/bin/sh\necho 'no such device: Scarlett' >&2\nexit 1\n"


class EngineRuns(unittest.TestCase):
    def test_engine_runs_when_installed_and_valid(self):
        done = run([], GOOD)
        self.assertEqual(0, done.returncode)
        self.assertEqual("", done.stderr)

    def test_reaper_runs_when_the_engine_is_missing(self):
        done = run([])
        self.assertEqual(1, done.returncode)
        self.assertIn("not installed", done.stderr)

    def test_reaper_runs_when_the_config_is_invalid(self):
        done = run([], BAD)
        self.assertEqual(1, done.returncode)
        self.assertIn("no such device: Scarlett", done.stderr)

    def test_reaper_flag_is_the_inverse(self):
        self.assertEqual(1, run(["--reaper"], GOOD).returncode)
        self.assertEqual(0, run(["--reaper"], BAD).returncode)
        self.assertEqual(0, run(["--reaper"]).returncode)

    def test_unknown_argument_is_refused(self):
        done = run(["--engine"], GOOD)
        self.assertEqual(2, done.returncode)
        self.assertIn("usage", done.stderr.lower())


class BrokenCheckFallsBackToReaper(unittest.TestCase):
    """For ExecCondition, 1 means skip: a crash must never skip both."""

    def run_without_lib(self, args):
        with tempfile.TemporaryDirectory() as tmp:
            bin_dir = Path(tmp) / "bin"
            bin_dir.mkdir()
            copy = bin_dir / "a3-engine-runs"
            copy.write_text(TOOL.read_text())
            return subprocess.run([sys.executable, str(copy), *args],
                                  capture_output=True, text=True)

    def test_reaper_flag_exits_0_when_the_module_is_missing(self):
        done = self.run_without_lib(["--reaper"])
        self.assertEqual(0, done.returncode)
        self.assertIn("a3-engine-runs: internal error, falling back to REAPER:",
                      done.stderr)

    def test_engine_form_exits_1_when_the_module_is_missing(self):
        done = self.run_without_lib([])
        self.assertEqual(1, done.returncode)
        self.assertIn("a3-engine-runs: internal error, falling back to REAPER:",
                      done.stderr)


if __name__ == "__main__":
    unittest.main()
