"""The install script's last step shows the network and headless questions.

a3-core_install.sh exports DEBIAN_FRONTEND=noninteractive for its apt steps,
and its closing `dpkg-reconfigure a3-core` inherited it: debconf took the
stored or default answers and asked nothing, so a fresh machine came up
without the a3 network (a3-core#62). Run as `wget ... | sudo bash`, its stdin
is the script itself, not a terminal, and debconf's terminal frontends need
one -- so the step reads from /dev/tty, and says what to run when there is
none.

Only the closing step is run, after the script's own export, with a fake
dpkg-reconfigure on PATH -- never the script itself.
"""

import os
import re
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = (Path(__file__).resolve().parents[2]
          / "platform-config/debian-x86_64/a3-core_install.sh")
LAST_STEP_STARTS = 'echo "Configure Network."'


def last_step():
    text = SCRIPT.read_text()
    if LAST_STEP_STARTS not in text:
        raise AssertionError("the script has no network step")
    return text[text.index(LAST_STEP_STARTS):]


class LastStep(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        fake = Path(self.tmp.name) / "dpkg-reconfigure"
        fake.write_text('#!/bin/sh\n'
                        'echo "frontend=${DEBIAN_FRONTEND:-}"\n'
                        '[ -t 0 ] && echo "stdin=tty"\n'
                        'exit 0\n')
        fake.chmod(0o755)
        self.env = dict(os.environ, PATH=f"{self.tmp.name}:{os.environ['PATH']}")
        self.step = ("set -euo pipefail\nexport DEBIAN_FRONTEND=noninteractive\n"
                     + last_step())

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_apt_steps_stay_unattended(self):
        lines = [l for l in SCRIPT.read_text().splitlines()
                 if re.match(r"^export DEBIAN_FRONTEND=", l)]
        self.assertEqual(["export DEBIAN_FRONTEND=noninteractive"], lines)

    def test_with_a_terminal_it_asks_there(self):
        # `script` gives the step a terminal; its stdin is not that terminal,
        # as under `wget ... | sudo bash`.
        done = subprocess.run(
            ["script", "-qec", f"bash -c {shlex.quote(self.step)} </dev/null",
             "/dev/null"],
            input="", env=self.env, capture_output=True, text=True, check=True)
        self.assertIn("stdin=tty", done.stdout)
        self.assertNotIn("frontend=noninteractive", done.stdout)

    def test_without_a_terminal_it_says_what_to_run(self):
        done = subprocess.run(["setsid", "-w", "bash", "-c", self.step],
                              input="", env=self.env, capture_output=True,
                              text=True, check=True)
        self.assertNotIn("frontend=", done.stdout)
        self.assertIn("dpkg-reconfigure a3-core", done.stdout + done.stderr)
        self.assertIn("by hand", done.stdout + done.stderr)


if __name__ == "__main__":
    unittest.main()
