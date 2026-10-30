"""The network is asked on the first install, or on dpkg-reconfigure.

Maintainer, 2026-10-31: "the installer asks about the network every time --
that should only happen initially, or with an argument." The package asked
"configure networking?" on every install (a reset to `no` each time, decided
2026-10-01 so one "yes" did not walk every later install through the setup).
Now the postinst remembers that the question was put (a3-core/network-asked)
and asks again only under dpkg-reconfigure. A machine that already has the
Core's network unit counts as asked. An unattended install does not count:
nobody saw the question.
"""

import unittest

from tools.tests.paths import DEBIAN
from tools.tests.test_package_network_step_can_be_left import NetworkStep, function

TEMPLATES = DEBIAN / "templates"
POSTINST = DEBIAN / "postinst"


class FirstInstall(NetworkStep):
    def test_the_question_is_asked(self):
        calls = self.run_step()
        self.assertIn("input high a3-core/configure-network", calls)

    def test_it_is_remembered_as_asked(self):
        self.run_step()
        self.assertEqual("true", self.stored("network-asked"))

    def test_esc_does_not_count_as_asked(self):
        self.run_step(esc_at="a3-core/configure-network")
        self.assertNotEqual("true", self.stored("network-asked"))

    def test_an_unattended_install_does_not_count(self):
        self.run_step(DEBIAN_FRONTEND="noninteractive")
        self.assertNotEqual("true", self.stored("network-asked"))


class LaterInstalls(NetworkStep):
    def test_an_asked_network_is_not_asked_again(self):
        self.store(network_asked="true", configure_network="true")
        calls = self.run_step()
        self.assertFalse([c for c in calls if c.startswith("input")], calls)
        self.assertNothingWritten()

    def test_a_machine_with_the_cores_unit_counts_as_asked(self):
        (self.net / "10-a3.network").write_text("[Match]\nName=eno1\n")
        calls = self.run_step()
        self.assertFalse([c for c in calls if c.startswith("input")], calls)
        self.assertEqual("[Match]\nName=eno1\n", (self.net / "10-a3.network").read_text())

    def test_the_installers_marked_answers_are_still_taken(self):
        self.store(network_asked="true", configure_network="true",
                   install_default_network="false", interface="eno1",
                   address="192.168.8.20/24", gateway="192.168.8.1",
                   dns="192.168.8.1", audio_port="", audio_address="",
                   preseeded="true")
        self.run_step()
        self.assertIn("Address=192.168.8.20/24", (self.net / "10-a3.network").read_text())


class Reconfigure(NetworkStep):
    def test_dpkg_reconfigure_asks_again(self):
        self.store(network_asked="true")
        calls = self.run_step(RECONFIGURING="true")
        self.assertIn("input high a3-core/configure-network", calls)


class Wiring(unittest.TestCase):
    def test_the_marker_is_a_template(self):
        self.assertIn("Template: a3-core/network-asked\nType: boolean\nDefault: false",
                      TEMPLATES.read_text())

    def test_reconfigure_is_detected_from_the_script_arguments(self):
        self.assertRegex(POSTINST.read_text(), r'"\$1" = reconfigure')
        self.assertIn("RECONFIGURING", function("network_question_due"))


if __name__ == "__main__":
    unittest.main()
