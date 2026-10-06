"""x11vnc runs without a password: the maintainer's decision (2026-10-05).

A password was added the same day and taken out again: VNC is reached only
inside the A³ LAN and through the Vigor's forwards from the home network, and
a unit that refuses to start without ~/.vnc/passwd left the rig without its
screen after a reboot. At a venue the VNC forwards are switched off."""

import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config/debian-x86_64/a3-core"
UNIT = PACKAGE / "etc/systemd/system/x11vnc.service"
POSTINST = PACKAGE / "DEBIAN/postinst"


class VncStartsWithoutAPassword(unittest.TestCase):
    def test_it_asks_for_no_password_file(self):
        self.assertNotIn("-rfbauth", UNIT.read_text())

    def test_nothing_stops_it_starting_without_one(self):
        for guard in ("AssertFileNotEmpty", "ConditionPathExists=/home/aaa/.vnc"):
            self.assertNotIn(guard, UNIT.read_text())


class VncIsAlwaysOn(unittest.TestCase):
    """2026-10-06: the rig came up with the old unit (password assert), the
    upgrade without it only enabled the unit, and VNC stayed off all day."""

    def test_an_upgrade_starts_it_not_only_enables_it(self):
        self.assertIn("systemctl restart x11vnc.service", POSTINST.read_text())

    def test_it_comes_back_after_any_exit(self):
        self.assertIn("Restart=always", UNIT.read_text())


if __name__ == "__main__":
    unittest.main()
