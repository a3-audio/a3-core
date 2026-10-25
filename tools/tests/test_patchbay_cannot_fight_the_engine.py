# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""The patchbay must not fight the engine (spec part C).

Reads a3-patchbay.xml read-only. qjackctl's patchbay reconnects what it lists,
and with exclusive="on" it disconnects everything else -- which would cut the
links the engine makes. So: no socket is exclusive, and none names the engine.
"""

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATCHBAY = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
            / "share/a3-core/config/rncbc.org/a3-patchbay.xml")


def sockets():
    return list(ET.parse(PATCHBAY).getroot().iter("socket"))


class PatchbayAndEngine(unittest.TestCase):
    def test_there_are_sockets_to_check(self):
        self.assertGreater(len(sockets()), 0)

    def test_no_socket_is_exclusive(self):
        exclusive = [s.get("name") for s in sockets() if s.get("exclusive") != "off"]
        self.assertEqual([], exclusive)

    def test_no_socket_client_matches_the_engine(self):
        clients = [s.get("client") for s in sockets()
                   if "a3-engine" in s.get("client", "").lower()]
        self.assertEqual([], clients)


if __name__ == "__main__":
    unittest.main()
