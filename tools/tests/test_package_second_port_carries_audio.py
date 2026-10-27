"""The Core's second network socket carries the audio network, not a bridge.

From 2026-09-25 the two sockets were bridged (br0 with STP) so the mixer could
hang on the second one. Every carrier drop on the first socket then cost the
whole bridge its forwarding for some 30 seconds of STP, and StemDeck went silent
with it. Since 2026-10-14 the sockets are two networks: the first keeps the
Core's address, gateway and DNS; the second is the AES67/PTP network, a subnet
of its own without gateway, so a reply always leaves by the port it came in on.
The mixer sits on the first network (192.168.8.11).

An install that finds the bridge files retires them (kept as .bak-<time>), so
upgrading a bridged Core is also the way out of the bridge.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN

POSTINST = DEBIAN / "postinst"
TEMPLATES = DEBIAN / "templates"

MAIN = "10-a3.network"
AUDIO = "20-a3-aes67.network"
RETIRED = ("a3.network", "10-a3-bridge.netdev", "20-a3-bridge-ports.network",
           "30-a3-bridge-address.network")


def function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                     re.MULTILINE | re.DOTALL)
    return body.group(0) if body else None


def run(name, *args):
    body = function(name)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return subprocess.run(["sh", "-c", f'{body}\n{name} "$@"', "sh", *args],
                          check=True, capture_output=True, text=True).stdout


def ini(path):
    """key=value pairs of a systemd unit file, sections flattened."""
    pairs = {}
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if line and not line.startswith(("#", "[")) and "=" in line:
            key, value = line.split("=", 1)
            pairs[key] = value
    return pairs


def write(directory, audio_port="enp5s0", audio_address="192.168.9.10/24"):
    run("write_network_config", str(directory), "eno1", "192.168.8.10/24",
        "192.168.8.1", "192.168.8.1", audio_port, audio_address)


class InTempDir(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def names(self):
        return sorted(p.name for p in self.dir.iterdir() if ".bak-" not in p.name)


class TwoSocketsTwoNetworks(InTempDir):
    def test_the_first_socket_keeps_address_gateway_and_dns(self):
        write(self.dir)
        main = ini(self.dir / MAIN)
        self.assertEqual("eno1", main["Name"])
        self.assertEqual("192.168.8.10/24", main["Address"])
        self.assertEqual("192.168.8.1", main["Gateway"])
        self.assertEqual("192.168.8.1", main["DNS"])

    def test_the_second_socket_is_the_audio_network(self):
        write(self.dir)
        audio = ini(self.dir / AUDIO)
        self.assertEqual("enp5s0", audio["Name"])
        self.assertEqual("192.168.9.10/24", audio["Address"])

    def test_the_audio_network_has_no_way_out(self):
        """The only default route is the first socket's."""
        write(self.dir)
        audio = ini(self.dir / AUDIO)
        self.assertNotIn("Gateway", audio)
        self.assertNotIn("DNS", audio)

    def test_nothing_else_is_written(self):
        write(self.dir)
        self.assertEqual([MAIN, AUDIO], self.names())


class OneSocket(InTempDir):
    def test_without_a_second_socket_only_the_first_is_written(self):
        write(self.dir, audio_port="", audio_address="")
        self.assertEqual([MAIN], self.names())

    def test_answering_none_takes_the_audio_network_away_again(self):
        write(self.dir)
        write(self.dir, audio_port="", audio_address="")
        self.assertEqual([MAIN], self.names())


class TheBridgeIsRetired(InTempDir):
    def a_bridged_core(self):
        for name in RETIRED:
            (self.dir / name).write_text("[Match]\nName=br0\n")

    def test_the_bridge_files_and_the_old_single_file_go(self):
        self.a_bridged_core()
        write(self.dir)
        self.assertEqual([MAIN, AUDIO], self.names())

    def test_each_is_kept_beside_as_a_backup(self):
        self.a_bridged_core()
        write(self.dir)
        backups = [p.name for p in self.dir.iterdir() if ".bak-" in p.name]
        for name in RETIRED:
            self.assertEqual(1, len([b for b in backups if b.startswith(name + ".bak-")]), name)

    def test_no_bridge_is_written_any_more(self):
        self.assertNotIn("Kind=bridge", POSTINST.read_text())
        self.assertNotIn("Bridge=br0", POSTINST.read_text())


class TheSecondSocketIsFound(unittest.TestCase):
    """What the question is pre-filled with the first time: the other wired
    socket on this machine. Never the first one, never Wi-Fi, never a virtual
    interface."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sys = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def iface(self, name, physical=True, wireless=False):
        path = self.sys / name
        path.mkdir()
        if physical:
            (path / "device").mkdir()
        if wireless:
            (path / "wireless").mkdir()

    def found(self, primary="eno1"):
        return run("other_wired_interface", primary, str(self.sys)).strip()

    def test_the_other_wired_socket(self):
        self.iface("lo", physical=False)
        self.iface("eno1")
        self.iface("enp5s0")
        self.assertEqual("enp5s0", self.found())

    def test_not_wifi_and_not_virtual(self):
        self.iface("eno1")
        self.iface("wlp2s0", wireless=True)
        self.iface("br0", physical=False)
        self.assertEqual("", self.found())

    def test_nothing_when_there_is_only_one(self):
        self.iface("eno1")
        self.assertEqual("", self.found())


class TheQuestions(unittest.TestCase):
    def block(self, name):
        blocks = TEMPLATES.read_text().split("\n\n")
        block = next((b for b in blocks if f"Template: a3-core/{name}\n" in b), None)
        self.assertIsNotNone(block, f"templates has no a3-core/{name}")
        return block

    def test_the_audio_port_is_a_string(self):
        self.assertIn("Type: string", self.block("audio-port"))

    def test_the_audio_address_defaults_to_the_audio_subnet(self):
        block = self.block("audio-address")
        self.assertIn("Type: string", block)
        self.assertIn("Default: 192.168.9.10/24", block)

    def test_the_bridge_question_is_gone(self):
        self.assertNotIn("a3-core/bridge-with", TEMPLATES.read_text())
        self.assertNotIn("bridge-with", POSTINST.read_text())

    def test_both_are_asked_every_time_where_they_are_seen(self):
        text = POSTINST.read_text()
        for name in ("audio-port", "audio-address"):
            unseen = text.find(f"db_fset a3-core/{name} seen false")
            shown = text.find(f"db_input high a3-core/{name}")
            self.assertNotEqual(-1, unseen, name)
            self.assertNotEqual(-1, shown, name)
            self.assertLess(unseen, shown, name)

    def test_the_postinst_writes_through_the_function(self):
        self.assertRegex(POSTINST.read_text(), r"\n\s+write_network_config ")


if __name__ == "__main__":
    unittest.main()
