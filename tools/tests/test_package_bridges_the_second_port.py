"""The Core can bridge its two network sockets.

The router has one free port, so the mixer hangs on the Core's second socket.
A bridge makes both sockets one segment: router in one, mixer in the other,
all on 192.168.8.0/24, the Core keeping 192.168.8.10. It is in the package
rather than on the box, because the postinst writes the network on every
install -- a hand-made bridge would have been overwritten, or worse, kept
while the address question went on writing a file nobody read.

The files are the ones prepared on 2026-09-09 (.claude/notes/2026-09-09-core-bridge):
br0 with STP on, both sockets as ports, the address on br0. Nothing is
switched during the install: networkd reads them at the next boot.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64" / "a3-core"
POSTINST = PACKAGE / "DEBIAN" / "postinst"
TEMPLATES = PACKAGE / "DEBIAN" / "templates"

BRIDGE_FILES = ("10-a3-bridge.netdev", "20-a3-bridge-ports.network",
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


class WithABridge(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        (self.dir / "a3.network").write_text("[Match]\nName=eno1\n")
        run("write_network_config", str(self.dir), "eno1", "enp5s0",
            "192.168.8.10/24", "192.168.8.1", "192.168.8.1")

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_three_bridge_files_are_written(self):
        for name in BRIDGE_FILES:
            self.assertTrue((self.dir / name).is_file(), name)

    def test_the_single_socket_file_is_gone(self):
        """networkd takes the first match; a3.network also matches eno1."""
        self.assertFalse((self.dir / "a3.network").exists())

    def test_br0_is_a_bridge_with_spanning_tree(self):
        netdev = ini(self.dir / "10-a3-bridge.netdev")
        self.assertEqual("br0", netdev["Name"])
        self.assertEqual("bridge", netdev["Kind"])
        self.assertEqual("yes", netdev["STP"])

    def test_both_sockets_are_ports(self):
        ports = ini(self.dir / "20-a3-bridge-ports.network")
        self.assertEqual({"eno1", "enp5s0"}, set(ports["Name"].split()))
        self.assertEqual("br0", ports["Bridge"])

    def test_the_address_lives_on_the_bridge(self):
        address = ini(self.dir / "30-a3-bridge-address.network")
        self.assertEqual("br0", address["Name"])
        self.assertEqual("192.168.8.10/24", address["Address"])
        self.assertEqual("192.168.8.1", address["Gateway"])
        self.assertEqual("192.168.8.1", address["DNS"])
        self.assertEqual("yes", address["ConfigureWithoutCarrier"])


class WithoutABridge(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_one_socket_one_file_as_before(self):
        run("write_network_config", str(self.dir), "eno1", "",
            "192.168.8.10/24", "192.168.8.1", "192.168.8.1")
        network = ini(self.dir / "a3.network")
        self.assertEqual("eno1", network["Name"])
        self.assertEqual("192.168.8.10/24", network["Address"])
        for name in BRIDGE_FILES:
            self.assertFalse((self.dir / name).exists(), name)

    def test_answering_no_bridge_takes_the_bridge_away_again(self):
        run("write_network_config", str(self.dir), "eno1", "enp5s0",
            "192.168.8.10/24", "192.168.8.1", "192.168.8.1")
        run("write_network_config", str(self.dir), "eno1", "",
            "192.168.8.10/24", "192.168.8.1", "192.168.8.1")
        self.assertTrue((self.dir / "a3.network").is_file())
        for name in BRIDGE_FILES:
            self.assertFalse((self.dir / name).exists(), name)


class TheSecondSocketIsFound(unittest.TestCase):
    """What the question is pre-filled with the first time: the other wired
    socket on this machine. Never the first one, never Wi-Fi, never a virtual
    interface -- a bridge onto lo or onto br0 itself would cut the box off."""

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


class TheQuestion(unittest.TestCase):
    def test_it_is_a_string_template(self):
        blocks = TEMPLATES.read_text().split("\n\n")
        block = next((b for b in blocks
                      if "Template: a3-core/bridge-with" in b), None)
        self.assertIsNotNone(block, "templates has no a3-core/bridge-with")
        self.assertIn("Type: string", block)

    def test_it_is_asked_every_time_where_it_is_seen(self):
        text = POSTINST.read_text()
        unseen = text.find('db_fset a3-core/bridge-with seen false')
        shown = text.find('db_input high a3-core/bridge-with')
        self.assertNotEqual(-1, unseen)
        self.assertNotEqual(-1, shown)
        self.assertLess(unseen, shown)

    def test_the_postinst_writes_through_the_function(self):
        self.assertRegex(POSTINST.read_text(), r"\n\s+write_network_config ")


if __name__ == "__main__":
    unittest.main()
