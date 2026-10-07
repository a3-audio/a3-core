"""REAPER's stop saves nothing (decided 2026-10-07, a3-system#74).

ExecStop used to save the running project over the template (-saveas
-template). With the outputs muted in the template and opened by Core, that
save would write them back open, and the next cold start would be loud again.
Core is the truth for every controlled value and replays it at start; what is
set only in REAPER the maintainer saves on purpose.

Because a stop no longer overwrites the template, the postinst has no reason
to stop and start REAPER around a template change, and it restarts nothing:
the maintainer restarts a3-main himself after an upgrade.
"""

import re
import unittest
from pathlib import Path

PACKAGE = (Path(__file__).resolve().parents[2]
           / "platform-config/debian-x86_64/a3-core")
UNIT = (PACKAGE / "home/aaa/.local/share/a3-core"
        / "config/systemd/user/a3-reaper.service")
POSTINST = PACKAGE / "DEBIAN/postinst"
TEMPLATE = "/home/aaa/.config/REAPER/ProjectTemplates/a3-reaper.RPP"


def directives(name):
    return [line.split("=", 1)[1] for line in UNIT.read_text().splitlines()
            if line.startswith(name + "=")]


class TheStopSavesNothing(unittest.TestCase):
    def test_no_save_as(self):
        for line in directives("ExecStop"):
            self.assertNotIn("-saveas", line)

    def test_the_template_is_not_named_on_stop(self):
        for line in directives("ExecStop"):
            self.assertNotIn("a3-reaper.RPP", line)

    def test_the_start_still_opens_the_template(self):
        self.assertTrue(any(f"-template {TEMPLATE}" in line
                            for line in directives("ExecStart")))


class ThePostinstRestartsNothing(unittest.TestCase):
    CONTROL = re.compile(
        r"systemctl\b.*\b(restart|try-restart|reload-or-restart|stop|start)\b"
        r".*\ba3-(reaper|main|core)\b|"
        r"user_systemctl\s+(restart|try-restart|reload-or-restart|stop|start)"
        r"\s+a3-(reaper|main|core)\b")

    def test_no_service_of_the_rig_is_controlled(self):
        for number, line in enumerate(POSTINST.read_text().splitlines(), 1):
            if line.lstrip().startswith("#") or line.lstrip().startswith("echo"):
                continue
            self.assertIsNone(self.CONTROL.search(line),
                              f"postinst line {number}: {line.strip()}")

    def test_it_says_the_maintainer_restarts(self):
        self.assertIn("restart a3-main", POSTINST.read_text())


if __name__ == "__main__":
    unittest.main()
