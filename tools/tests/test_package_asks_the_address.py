"""The install asks for the network address, every time.

The address questions ran at debconf priority `medium`, which Debian does not
show by default, and a question once answered is never asked again. So the
answer given once -- 192.168.43.57 on the rig, from before it moved subnet --
went out silently with every install, and a3.network came back on the old
network after the fix for exactly that had been installed (2026-09-25).

Decided with the maintainer: the address, gateway and DNS are asked on every
install and upgrade, at `high`, pre-filled with what is stored -- Enter keeps
it. An unattended install (noninteractive frontend) still takes the stored
value without asking.
"""

import re
import subprocess
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64" / "a3-core"
POSTINST = PACKAGE / "DEBIAN" / "postinst"

# Stand-ins for debconf's shell commands that write down how they were called.
STUBS = """
db_fset () { echo "fset $1 $2 $3"; }
db_input () { echo "input $1 $2"; }
db_go () { echo "go"; }
"""


class TheAddressIsAskedEveryTime(unittest.TestCase):
    def calls(self):
        body = re.search(r"^ask_address_questions\(\) \{.*?^\}$",
                         POSTINST.read_text(), re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(body, "postinst defines no ask_address_questions()")
        out = subprocess.run(["sh", "-c", STUBS + body.group(0)
                              + "\nask_address_questions"],
                             check=True, capture_output=True, text=True).stdout
        return out.splitlines()

    def test_each_is_shown_at_a_priority_debian_shows(self):
        calls = self.calls()
        for question in ("address", "gateway", "dns"):
            self.assertIn(f"input high a3-core/{question}", calls)

    def test_each_is_asked_again_even_when_answered_before(self):
        calls = self.calls()
        for question in ("address", "gateway", "dns"):
            unseen = f"fset a3-core/{question} seen false"
            shown = f"input high a3-core/{question}"
            self.assertIn(unseen, calls)
            self.assertLess(calls.index(unseen), calls.index(shown),
                            f"{question} is marked unseen only after it is shown")

    def test_the_questions_are_actually_put(self):
        self.assertEqual("go", self.calls()[-1])

    def test_the_postinst_uses_it(self):
        self.assertRegex(POSTINST.read_text(), r"\n\s+ask_address_questions\n")


class NetworkSetupIsOffered(unittest.TestCase):
    def test_whether_to_configure_the_network_is_asked_where_it_is_seen(self):
        """At `medium` a fresh machine never saw the question, answered it
        with its default (no), and never got a network at all."""
        self.assertRegex(POSTINST.read_text(),
                         r"db_input (high|critical) a3-core/configure-network")


if __name__ == "__main__":
    unittest.main()
