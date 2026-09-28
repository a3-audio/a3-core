"""zita-n2j listens on every interface, not on the loopback.

**Why this exists.** The shipped unit started it with `localhost 65100`.
zita-n2j resolves that to `[::1]`, the IPv6 loopback, so a stream sent to
the machine from the network never arrived -- nothing complained, the JACK
ports were there and silent. Found on the rig 2026-09-29, where the unit in
~/.config had been edited to 0.0.0.0 by hand while the package still shipped
localhost for every fresh install.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UNIT = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
        / "share/a3-core/config/systemd/user/zita-n2j.service")


def exec_start(unit):
    for line in unit.read_text().splitlines():
        if line.startswith("ExecStart="):
            return line[len("ExecStart="):].split()
    raise AssertionError(f"{unit.name} has no ExecStart")


class N2jListensOnTheNetwork(unittest.TestCase):
    def test_it_binds_every_interface(self):
        args = exec_start(UNIT)
        # The last two arguments are the address and the port.
        address, port = args[-2], args[-1]
        self.assertEqual(address, "0.0.0.0",
                         "a loopback address hears nothing from the network")
        self.assertTrue(re.fullmatch(r"\d+", port), port)


if __name__ == "__main__":
    unittest.main()
