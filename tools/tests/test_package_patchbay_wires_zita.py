"""The shipped patchbay wires the network audio into REAPER and back out.

**Why this exists.** 551dd2a (2026-09-24) shipped the February patchbay
from the rig, which predates zita and wires neither direction. The one the
maintainer wrote on 2026-09-28 -- the stem player's ten channels in through
zita-n2j, REAPER's recording bus out through zita-j2n -- lived only under
~/.local/share, where the next install replaced it with the shipped one.
Rewritten and shipped 2026-09-29.
"""

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATCHBAY = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
            / "share/a3-core/config/rncbc.org/a3-patchbay.xml")


def sockets_and_cables(path):
    root = ET.parse(path).getroot()
    sockets = {s.get("name"): s for s in root.iter("socket")}
    cables = {(c.get("output"), c.get("input")) for c in root.iter("cable")}
    return sockets, cables


def socket_client(sockets, name):
    # QjackCtl escapes '-' and ' ' in client names with a backslash.
    return sockets[name].get("client").replace("\\", "")


class PatchbayWiresZita(unittest.TestCase):
    def setUp(self):
        self.sockets, self.cables = sockets_and_cables(PATCHBAY)

    def cables_from(self, client):
        return [(out, into) for out, into in self.cables
                if out in self.sockets and socket_client(self.sockets, out) == client]

    def cables_into(self, client):
        return [(out, into) for out, into in self.cables
                if into in self.sockets and socket_client(self.sockets, into) == client]

    def test_zita_n2j_plays_into_reaper(self):
        wired = [into for _, into in self.cables_from("zita-n2j")]
        self.assertTrue(any(socket_client(self.sockets, s) == "REAPER" for s in wired),
                        "nothing from the network reaches REAPER")

    def test_every_wired_n2j_channel_has_a_reaper_input(self):
        (out, into), = [c for c in self.cables_from("zita-n2j")]
        self.assertEqual(len(self.sockets[into].findall("plug")),
                         len(self.sockets[out].findall("plug")))

    def test_reaper_records_out_through_zita_j2n(self):
        sources = [out for out, _ in self.cables_into("zita-j2n")]
        self.assertTrue(any(socket_client(self.sockets, s) == "REAPER" for s in sources),
                        "REAPER's recording bus goes nowhere on the network")
