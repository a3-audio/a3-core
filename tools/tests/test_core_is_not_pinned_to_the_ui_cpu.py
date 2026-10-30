"""Core may run on any CPU, not only on the one the Motion UI draws on.

Both units said CPUAffinity=0. On the rig on 2026-09-26 that put Core (about
27 % of a CPU, at 1,024 positions a second from Motion) on the same CPU as the
UI's OpenGL renderer (about 50 %) and message thread (about 12 %). Whenever the
UI peaked, Core got no time, the queue on port 9000 filled to 160 KB and the
kernel dropped 14,461 packets in seven minutes. Unpinned live, three minutes of
play: no drop, the queue peaked at 8.8 KB, and no crackle -- the audio threads
on CPUs 1-3 run real-time, so an ordinary Core only takes what they leave.

It first shipped as a drop-in (a3-core.service.d/cpu.conf) that reset the
mask of the deployment's copy in ~/.config. Since 2026-10-31 there is one
a3-core.service, a3-core-app's, and it never pinned: neither it nor anything
packaged on top of it names CPUAffinity.
"""

import unittest

from tools.tests.paths import DEPLOYMENT_UNITS, ROOT

UNIT = ROOT / "packaging" / "a3-core.service"


def service_lines(path):
    lines = [line.strip() for line in path.read_text().splitlines()]
    return [line for line in lines if line and not line.startswith("#")]


class CoreIsNotPinned(unittest.TestCase):
    def test_the_unit_does_not_pin(self):
        self.assertEqual([], [l for l in service_lines(UNIT) if l.startswith("CPUAffinity")])

    def test_no_packaged_drop_in_pins_it(self):
        self.assertEqual([], sorted(DEPLOYMENT_UNITS.glob("a3-core.service.d/*")))


if __name__ == "__main__":
    unittest.main()
