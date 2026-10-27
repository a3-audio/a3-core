"""An install leaves a3-main enabled, so the rig comes up at the next login.

a3-main wants qjackctl, REAPER, Core, the analyzer and Motion. The postinst
copies the shipped units into ~/.config as files only -- a `.wants` link
never travels with them -- and replacing the systemd group took the link
that enabled a3-main away: after the install on 2026-10-14 nothing would
have started REAPER or Core at the next boot. The postinst now enables
a3-main itself, in aaa's user manager, once it has the units in place.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN

POSTINST = DEBIAN / "postinst"


def postinst_function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                     re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


class TheRigIsEnabled(unittest.TestCase):
    def run_enable(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "log"
            script = (f'user_systemctl() {{ echo "user $*" >> {log}; }}\n'
                      f'{postinst_function("enable_the_rig")}\n'
                      "enable_the_rig\necho went-on\n")
            done = subprocess.run(["sh", "-c", f"set -e\n{script}"],
                                  capture_output=True, text=True)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertIn("went-on", done.stdout)
            return log.read_text() if log.exists() else ""

    def test_a3_main_is_enabled_for_the_user(self):
        self.assertIn("user enable a3-main.service", self.run_enable())

    def test_it_runs_in_the_user_install_after_the_units_are_loaded(self):
        body = postinst_function("run_user_install")
        self.assertIn("enable_the_rig", body)
        self.assertLess(body.index("daemon-reload"), body.index("enable_the_rig"))


if __name__ == "__main__":
    unittest.main()
