"""The packaged a3-core.service runs Python unbuffered (issue #69).

Without it, Core's log lines sit in stdout's buffer when stdout is the
journal's pipe, so `journalctl -u a3-core | grep 'gate:'` shows nothing until
the buffer fills. The unit sets PYTHONUNBUFFERED=1 rather than passing -u: the
variable also covers any Python child Core starts, and ExecStart stays a plain
interpreter-plus-script line.
"""

import re
import unittest
from pathlib import Path

UNIT = (Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64"
        / "a3-core" / "home" / "aaa" / ".local" / "share" / "a3-core" / "config"
        / "systemd" / "user" / "a3-core.service")


class CoreUnitRunsUnbuffered(unittest.TestCase):
    def test_unit_sets_pythonunbuffered(self):
        text = UNIT.read_text()
        self.assertRegex(text, re.compile(
            r"^Environment=.*\bPYTHONUNBUFFERED=1\b", re.M))


if __name__ == "__main__":
    unittest.main()
