"""x11vnc runs without a password: the maintainer's decision (2026-10-05).

A password was added the same day and taken out again: VNC is reached only
inside the A³ LAN and through the Vigor's forwards from the home network, and
a unit that refuses to start without ~/.vnc/passwd left the rig without its
screen after a reboot. At a venue the VNC forwards are switched off."""

import unittest
from pathlib import Path

UNIT = (Path(__file__).resolve().parents[2]
        / "platform-config/debian-x86_64/a3-core/etc/systemd/system/x11vnc.service")


class VncStartsWithoutAPassword(unittest.TestCase):
    def test_it_asks_for_no_password_file(self):
        self.assertNotIn("-rfbauth", UNIT.read_text())

    def test_nothing_stops_it_starting_without_one(self):
        for guard in ("AssertFileNotEmpty", "ConditionPathExists=/home/aaa/.vnc"):
            self.assertNotIn(guard, UNIT.read_text())


if __name__ == "__main__":
    unittest.main()
