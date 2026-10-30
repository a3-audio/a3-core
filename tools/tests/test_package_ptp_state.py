# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""ptp4l's port state for the clock role, read as root (2026-10-30).

On the Core, Debian's ptp4l@ unit is sandboxed and ptp4l 4.4 never answers a
user's pmc, whatever the client socket (measured on a3nuc1: /tmp, /dev/shm,
umask 000). The clock role needs to know that ptp4l is the grandmaster before
it says LOCKED. So a3-ptp-state.timer runs a3-ptp-state as root every 10 s:
`pmc -u -b 0 'GET PORT_DATA_SET'` on ptp4l's own socket, its portState written
atomically as /run/a3/ptp-state.json (0644). The clock role reads the file
(src/a3-clock/ClockPtpState.h).
"""

import json
import os
import re
import stat
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

from tools.tests.paths import APP_RECIPE, DEBIAN, PACKAGE

# The script is a program, so a3-core-app ships it (/usr/lib/a3-core); the units are the
# Core machine's, a3-core's, beside its other system units (conffiles).
SCRIPT = APP_RECIPE / "a3-ptp-state"
SERVICE = PACKAGE / "etc/systemd/system/a3-ptp-state.service"
TIMER = PACKAGE / "etc/systemd/system/a3-ptp-state.timer"
POSTINST = DEBIAN / "postinst"

ANSWER = """sending: GET PORT_DATA_SET
\ta0369f.fffe.1d2e3f-1 seq 0 RESPONSE MANAGEMENT PORT_DATA_SET
\t\tportIdentity            a0369f.fffe.1d2e3f-1
\t\tportState               MASTER
\t\tlogMinDelayReqInterval  0
"""


def settings(path):
    return [l.strip() for l in path.read_text().splitlines()
            if l.strip() and not l.strip().startswith("#")]


def function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(), re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


class TheUnits(unittest.TestCase):
    def test_the_service_runs_the_script_once_as_root(self):
        self.assertEqual(settings(SERVICE), [
            "[Unit]",
            "Description=a3-ptp-state, ptp4l's port state for the clock role",
            "After=network.target",
            "[Service]",
            "Type=oneshot",
            "ExecStart=/usr/lib/a3-core/a3-ptp-state",
        ])

    def test_the_timer_runs_it_every_ten_seconds(self):
        self.assertEqual(settings(TIMER), [
            "[Unit]",
            "Description=a3-ptp-state every 10 s",
            "[Timer]",
            "OnBootSec=10s",
            "OnUnitActiveSec=10s",
            "AccuracySec=1s",
            "[Install]",
            "WantedBy=timers.target",
        ])

    def test_the_script_is_executable(self):
        self.assertTrue(SCRIPT.stat().st_mode & stat.S_IXUSR)

    def test_the_app_installs_the_script(self):
        self.assertIn('"$src/$RECIPE/a3-ptp-state" "$stage/usr/lib/a3-core/a3-ptp-state"',
                      (APP_RECIPE / "stage").read_text())

    def test_the_units_are_conffiles(self):
        entries = (DEBIAN / "conffiles").read_text().splitlines()
        self.assertIn("/etc/systemd/system/a3-ptp-state.service", entries)
        self.assertIn("/etc/systemd/system/a3-ptp-state.timer", entries)


class TheScript(unittest.TestCase):
    """Run with a fake pmc on PATH and the file in a scratch directory."""

    def run_it(self, answer, exit_code=0):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        bin_dir = Path(tmp.name) / "bin"
        bin_dir.mkdir()
        (bin_dir / "pmc").write_text(f"#!/bin/sh\ncat <<'EOF'\n{answer}EOF\nexit {exit_code}\n")
        (bin_dir / "pmc").chmod(0o755)
        out = Path(tmp.name) / "run/a3/ptp-state.json"
        done = subprocess.run([str(SCRIPT)], env={"PATH": f"{bin_dir}:/usr/bin:/bin", "A3_PTP_STATE_FILE": str(out)},
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        return out

    def test_a_master_is_written(self):
        before = time.time()
        out = self.run_it(ANSWER)
        state = json.loads(out.read_text())
        self.assertEqual(state["portState"], "MASTER")
        self.assertEqual(state["problem"], "")
        self.assertLessEqual(abs(state["writtenAtUnix"] - before), 5)

    def test_no_answer_is_said(self):
        state = json.loads(self.run_it("sending: GET PORT_DATA_SET\n").read_text())
        self.assertEqual(state["portState"], "")
        self.assertIn("no answer from ptp4l", state["problem"])

    def test_it_is_written_whole_and_readable_by_all(self):
        out = self.run_it(ANSWER)
        self.assertEqual([p.name for p in out.parent.iterdir()], ["ptp-state.json"])   # no temporary left
        self.assertEqual(stat.S_IMODE(out.stat().st_mode), 0o644)
        self.assertEqual(stat.S_IMODE(out.parent.stat().st_mode), 0o755)

    def test_it_renames_a_temporary_over_the_file(self):
        text = SCRIPT.read_text()
        self.assertIn("mktemp", text)
        self.assertRegex(text, r"mv -f \"\$tmp\" \"\$out\"")

    def test_the_default_file_is_under_run(self):
        self.assertIn("/run/a3/ptp-state.json", SCRIPT.read_text())


class ThePostinstEnablesTheTimer(unittest.TestCase):
    def run_enable(self, audio_port):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "log"
            script = (
                'render_default_network() { printf "INTERFACE=eno1\\nAUDIO_PORT=%s\\n" "$TEST_PORT"; }\n'
                f'systemctl() {{ echo "system $*" >> {log}; }}\n'
                f'user_systemctl() {{ echo "user $*" >> {log}; }}\n'
                f'{function("audio_port")}\n{function("enable_aes67")}\n'
                "enable_aes67\n"
            )
            subprocess.run(["sh", "-c", f"set -e\n{script}"], env=dict(os.environ, TEST_PORT=audio_port),
                           check=True, capture_output=True, text=True)
            return log.read_text() if log.exists() else ""

    def test_with_an_audio_port_it_is_enabled_not_started(self):
        log = self.run_enable("enp5s0")
        self.assertIn("system enable a3-ptp-state.timer", log)
        self.assertNotRegex(log, r"(start|restart) a3-ptp-state")

    def test_without_one_it_is_disabled(self):
        self.assertIn("system disable a3-ptp-state.timer", self.run_enable(""))


if __name__ == "__main__":
    unittest.main()
