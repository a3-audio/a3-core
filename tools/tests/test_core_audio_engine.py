# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Engine or REAPER (engine plan 4, decision D22).

REAPER is frozen: there is no boot choice. The a3-engine runs when it is
installed and its configuration checks out; otherwise REAPER runs, with the
reason. The rule is here, without processes; probe() is the one place that
looks at the machine, tested against a fake script.
"""

import stat
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_audio_engine import engine_runs, probe   # noqa: E402


class Check:
    def __init__(self, result):
        self.result = result
        self.calls = 0

    def __call__(self):
        self.calls += 1
        return self.result


class TheRule(unittest.TestCase):
    def test_installed_and_valid_runs_the_engine(self):
        self.assertEqual(engine_runs(True, Check((True, ""))), (True, ""))

    def test_not_installed_runs_reaper_and_never_checks(self):
        check = Check((True, ""))
        self.assertEqual(engine_runs(False, check),
                         (False, "a3-engine not installed"))
        self.assertEqual(check.calls, 0)

    def test_invalid_config_runs_reaper_with_the_reason(self):
        self.assertEqual(engine_runs(True, Check((False, "no such device"))),
                         (False, "config invalid: no such device"))

    def test_check_timeout_runs_reaper(self):
        ok, why = engine_runs(True, Check((False, "check timed out after 5 s")))
        self.assertFalse(ok)
        self.assertIn("timed out", why)


class Probe(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def script(self, body, executable=True):
        path = self.dir / "a3-engine"
        path.write_text("#!/bin/sh\n" + body + "\n")
        path.chmod(path.stat().st_mode | (stat.S_IXUSR if executable else 0))
        if not executable:
            path.chmod(0o644)
        return path

    def test_probe_missing_binary_is_not_installed(self):
        installed, _ = probe(self.dir / "absent")
        self.assertFalse(installed)

    def test_probe_non_executable_is_not_installed(self):
        installed, _ = probe(self.script("exit 0", executable=False))
        self.assertFalse(installed)

    def test_probe_check_passes_on_exit_zero(self):
        installed, check = probe(self.script('[ "$1" = --check-config ] && exit 0; exit 3'))
        self.assertTrue(installed)
        self.assertEqual(check(), (True, ""))

    def test_probe_check_reports_the_last_line_on_failure(self):
        _, check = probe(self.script("echo first; echo 'bad rate' >&2; exit 2"))
        ok, why = check()
        self.assertFalse(ok)
        self.assertEqual(why, "bad rate")

    def test_probe_check_times_out(self):
        _, check = probe(self.script("sleep 5"), timeout=0.2)
        ok, why = check()
        self.assertFalse(ok)
        self.assertIn("timed out", why)


if __name__ == "__main__":
    unittest.main()
