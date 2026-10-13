"""The shipped patchbay wires the network audio into REAPER and back out.

**Why this exists.** 551dd2a (2026-09-24) shipped the February patchbay
from the rig, which wired the network in neither direction; the one the
maintainer wrote on 2026-09-28 lived only under ~/.local/share, where the
next install replaced it with the shipped one. The network audio was zita
until 2026-10-13 and is AES67 since: a remote StemDeck's stems come in
through an AES67 receiver, REAPER's recording bus goes out through this
Core's AES67 sender.
"""

import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATCHBAY = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
            / "share/a3-core/config/rncbc.org/a3-patchbay.xml")


def sockets_and_cables(path):
    """Output and input sockets apart: QjackCtl keeps two lists, and the same
    name may be an output and an input socket at once. A cable's `output`
    names an output socket, its `input` an input socket."""
    root = ET.parse(path).getroot()
    outputs = {s.get("name"): s for s in root.find("output-sockets").iter("socket")}
    inputs = {s.get("name"): s for s in root.find("input-sockets").iter("socket")}
    cables = {(c.get("output"), c.get("input")) for c in root.iter("cable")}
    return outputs, inputs, cables


def socket_client(sockets, name):
    # QjackCtl escapes '-' and ' ' in client names with a backslash.
    return sockets[name].get("client").replace("\\", "")


def plugs(socket):
    return [p.text for p in socket.findall("plug")]


def is_aes67(client):
    return client.startswith("AES67 ")


class StemDeckHasNoPhones(unittest.TestCase):
    """Maintainer, 2026-10-07: StemDeck's PHONES outputs are gone; a channel
    is cued through its own channel bus."""

    def setUp(self):
        self.outputs, self.inputs, self.cables = sockets_and_cables(PATCHBAY)

    def test_no_stemdeck_socket_carries_phones(self):
        for name, socket in self.outputs.items():
            if socket_client(self.outputs, name) == "StemDeck":
                self.assertNotIn("phones_L", plugs(socket), name)
                self.assertNotIn("phones_R", plugs(socket), name)


class StemDeckCueReachesReaper(unittest.TestCase):
    """StemDeck's cue output pair (2026-10-14) goes to REAPER in23-24, the
    stems track's cue inputs."""

    def setUp(self):
        self.outputs, self.inputs, self.cables = sockets_and_cables(PATCHBAY)

    def test_a_cue_pair_from_the_network_lands_on_in23_and_in24(self):
        cue = [into for out, into in self.cables
               if out in self.outputs and is_aes67(socket_client(self.outputs, out))
               and set(plugs(self.outputs[out])) == {"receive_CUEL", "receive_CUER"}]
        self.assertTrue(cue, "no cable carries StemDeck's cue pair")
        for into in cue:
            self.assertEqual(socket_client(self.inputs, into), "REAPER")
            self.assertEqual(plugs(self.inputs[into]), ["in23", "in24"])


class PatchbayWiresAes67(unittest.TestCase):
    def setUp(self):
        self.outputs, self.inputs, self.cables = sockets_and_cables(PATCHBAY)

    def cables_from_aes67(self):
        return [(out, into) for out, into in self.cables
                if out in self.outputs and is_aes67(socket_client(self.outputs, out))]

    def cables_into_aes67(self):
        return [(out, into) for out, into in self.cables
                if into in self.inputs and is_aes67(socket_client(self.inputs, into))]

    def test_an_aes67_receiver_plays_into_reaper(self):
        wired = [into for _, into in self.cables_from_aes67()]
        self.assertTrue(any(socket_client(self.inputs, s) == "REAPER" for s in wired),
                        "nothing from the network reaches REAPER")

    def test_every_wired_receive_channel_has_a_reaper_input(self):
        for out, into in self.cables_from_aes67():
            self.assertGreaterEqual(len(plugs(self.inputs[into])),
                                    len(plugs(self.outputs[out])), (out, into))

    def test_reaper_records_out_through_the_aes67_sender(self):
        sources = [out for out, _ in self.cables_into_aes67()]
        self.assertIn("reaper_rec", sources,
                      "REAPER's recording bus goes nowhere on the network")

    def test_no_socket_names_a_zita_client_of_the_core(self):
        # zita-n2j and zita-j2n are gone from the Core (2026-10-14); a socket
        # on them waits for a client that never comes.
        for sockets in (self.outputs, self.inputs):
            for name in sockets:
                self.assertNotIn(socket_client(sockets, name), ("zita-n2j", "zita-j2n"), name)


class TheStreamsFitAnyMachine(unittest.TestCase):
    """The AES67 streams are named after their machine ("AES67 <host> out",
    "AES67 <host> stems"), so the patchbay matches them by pattern -- QjackCtl
    reads a socket's client as a regular expression -- never by one machine's
    name (decided 2026-10-14)."""

    def aes67_clients(self):
        root = ET.parse(PATCHBAY).getroot()
        return [s.get("client") for s in root.iter("socket")
                if s.get("client").replace("\\", "").startswith("AES67")]

    def test_there_are_some(self):
        self.assertTrue(self.aes67_clients())

    def test_no_machine_is_named(self):
        for client in self.aes67_clients():
            self.assertNotIn("a3nuc", client)

    def test_another_machines_streams_are_matched_too(self):
        for client in self.aes67_clients():
            self.assertTrue(re.fullmatch(client, "AES67 studio-2 stems")
                            or re.fullmatch(client, "AES67 core-b out"), client)


if __name__ == "__main__":
    unittest.main()
