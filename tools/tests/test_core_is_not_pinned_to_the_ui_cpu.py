"""Core may run on any CPU, not only on the one the Motion UI draws on.

Both units said CPUAffinity=0. On the rig on 2026-09-26 that put Core (about
27 % of a CPU, at 1,024 positions a second from Motion) on the same CPU as the
UI's OpenGL renderer (about 50 %) and message thread (about 12 %). Whenever the
UI peaked, Core got no time, the queue on port 9000 filled to 160 KB and the
kernel dropped 14,461 packets in seven minutes. Unpinned live, three minutes of
play: no drop, the queue peaked at 8.8 KB, and no crackle -- the audio threads
on CPUs 1-3 run real-time, so an ordinary Core only takes what they leave.

It ships as a drop-in because the postinst copies the config with `cp -rn`,
which never overwrites: a changed line in a3-core.service would reach fresh
installs only, while a new file reaches every rig on the next upgrade.
"""

import unittest
from pathlib import Path

UNITS = (Path(__file__).resolve().parents[2]
         / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
         / "share/a3-core/config/systemd/user")
UNIT = UNITS / "a3-core.service"
DROP_IN = UNITS / "a3-core.service.d" / "cpu.conf"


def service_lines(path):
    lines = [line.strip() for line in path.read_text().splitlines()]
    return [line for line in lines if line and not line.startswith("#")]


class CoreIsNotPinned(unittest.TestCase):
    def test_the_drop_in_allows_every_cpu(self):
        """An empty CPUAffinity= resets the mask: every CPU the machine has,
        without this file having to know how many that is."""
        self.assertTrue(DROP_IN.is_file(), "no a3-core.service.d/cpu.conf shipped")
        lines = service_lines(DROP_IN)
        self.assertIn("[Service]", lines)
        self.assertEqual(["CPUAffinity="],
                         [l for l in lines if l.startswith("CPUAffinity")])

    def test_the_unit_itself_no_longer_pins(self):
        """A fresh install should read the same in the unit as it runs."""
        self.assertEqual([], [l for l in service_lines(UNIT)
                              if l.startswith("CPUAffinity")])


if __name__ == "__main__":
    unittest.main()
