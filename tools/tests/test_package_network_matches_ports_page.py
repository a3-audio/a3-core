"""The addresses the package ships are the ones a3-doc's ports page states.

The rig moved from 192.168.43.x to 192.168.8.x, and the package did not: the
postinst kept offering 192.168.43.58, debconf kept re-applying the last answer
on every update, and Core's peer addresses stayed at .61 and .62. The running
values lived only in a hand-made service override. See a3-core issue #54.

The documented values are written out here rather than read from a3-doc, so
this suite runs from a3-core alone. The page is the source; if it changes,
this list changes with it.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64" / "a3-core"
POSTINST = PACKAGE / "DEBIAN" / "postinst"
TEMPLATES = PACKAGE / "DEBIAN" / "templates"
CORE = PACKAGE / "home" / "aaa" / ".local" / "bin" / "a3-core.py"
VNC = PACKAGE / "home" / "aaa" / ".local" / "share" / "a3-core" / "recipes" / "a3vnc.sh"

# https://a3-audio.github.io/a3-doc/ressources/ports.html
CORE_ADDRESS = "192.168.8.10/24"
GATEWAY = "192.168.8.1"
MIXER = "192.168.8.11:7772"
# Motion's UI runs on the Core machine, and listens on 7771.
MOTION = "127.0.0.1:7771"


def template_default(name):
    blocks = TEMPLATES.read_text().split("\n\n")
    for block in blocks:
        if f"Template: a3-core/{name}" in block:
            match = re.search(r"^Default: (.*)$", block, re.MULTILINE)
            return match.group(1).strip() if match else None
    return None


def postinst_standard(name):
    """The value the postinst's "standard network" branch assigns to NAME."""
    match = re.search(rf'^\s*{name}="([^"]*)"', POSTINST.read_text(), re.MULTILINE)
    return match.group(1) if match else None


def core_default(option):
    """The default of an a3-core.py command line option, evaluated from the
    module's own constants rather than by importing it (importing starts
    servers)."""
    source = CORE.read_text()
    hosts = dict(re.findall(
        r"^(A3\w+_HOST), \w+_PORT = '([^']*)', \d+", source, re.MULTILINE))
    ports = dict(re.findall(
        r"^A3\w+_HOST, (A3\w+_PORT) = '[^']*', (\d+)", source, re.MULTILINE))
    match = re.search(
        rf'add_argument\("--{option}", default=f"\{{(\w+)\}}:\{{(\w+)\}}"', source)
    if not match:
        return None
    return f"{hosts.get(match.group(1))}:{ports.get(match.group(2))}"


class TheInstallerOffersTheDocumentedNetwork(unittest.TestCase):
    def test_the_templates_default_to_the_documented_address(self):
        self.assertEqual(CORE_ADDRESS, template_default("address"))
        self.assertEqual(GATEWAY, template_default("gateway"))
        self.assertEqual(GATEWAY, template_default("dns"))

    def test_the_standard_network_is_the_documented_one(self):
        self.assertEqual(CORE_ADDRESS, postinst_standard("ADDR"))
        self.assertEqual(GATEWAY, postinst_standard("GATEWAY"))
        self.assertEqual(GATEWAY, postinst_standard("DNS"))


class CoreSendsWhereThePortsPageSays(unittest.TestCase):
    def test_the_mixer(self):
        self.assertEqual(MIXER, core_default("mixer"))

    def test_motion(self):
        self.assertEqual(MOTION, core_default("motion"))


class VncLooksForTheCoreWhereItIs(unittest.TestCase):
    def test_the_viewer_points_at_the_core(self):
        self.assertIn(CORE_ADDRESS.split("/")[0], VNC.read_text())


class OneManagerOwnsTheInterface(unittest.TestCase):
    """ifupdown's DHCP stanza and systemd-networkd's static file both claimed
    eno1, so the address depended on which came up first. When the postinst
    writes a3.network it takes the interface out of ifupdown's hands."""

    INTERFACES = (
        "source /etc/network/interfaces.d/*\n"
        "\n"
        "auto lo\n"
        "iface lo inet loopback\n"
        "\n"
        "allow-hotplug eno1\n"
        "iface eno1 inet dhcp\n"
    )

    def run_function(self, interface, text):
        body = re.search(r"^disable_ifupdown_for\(\) \{.*?^\}$",
                         POSTINST.read_text(), re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(body, "postinst defines no disable_ifupdown_for()")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "interfaces"
            path.write_text(text)
            subprocess.run(["sh", "-c", f'{body.group(0)}\n'
                            f'disable_ifupdown_for "$1" "$2"', "sh",
                            interface, str(path)], check=True)
            return path.read_text()

    def active_lines(self, text):
        return [line.strip() for line in text.splitlines()
                if line.strip() and not line.lstrip().startswith("#")]

    def test_the_interface_leaves_ifupdown(self):
        after = self.active_lines(self.run_function("eno1", self.INTERFACES))
        self.assertNotIn("iface eno1 inet dhcp", after)
        self.assertNotIn("allow-hotplug eno1", after)

    def test_everything_else_stays(self):
        after = self.active_lines(self.run_function("eno1", self.INTERFACES))
        self.assertIn("auto lo", after)
        self.assertIn("iface lo inet loopback", after)
        self.assertIn("source /etc/network/interfaces.d/*", after)

    def test_another_interface_is_left_alone(self):
        text = self.INTERFACES.replace("eno1", "enp2s0")
        self.assertEqual(text, self.run_function("eno1", text))

    def test_running_it_twice_changes_nothing_more(self):
        once = self.run_function("eno1", self.INTERFACES)
        self.assertEqual(once, self.run_function("eno1", once))


if __name__ == "__main__":
    unittest.main()
