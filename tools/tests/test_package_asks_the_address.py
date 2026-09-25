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
db_reset () { echo "reset $1"; }
db_get () { RET=""; }
"""


def stored(values):
    """A db_get that answers from `values`, as debconf would from its store."""
    cases = "".join(f'    a3-core/{q}) RET="{v}" ;;\n' for q, v in values.items())
    return ("db_get () {\n  case \"$1\" in\n" + cases
            + '    *) RET="" ;;\n  esac\n}\n')


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


class ARetiredAnswerIsNotOffered(unittest.TestCase):
    """The rig moved from 192.168.43.x to 192.168.8.x. debconf pre-fills a
    question with the stored answer, not the template's default, so a machine
    that was set up on the old network was offered the old address first --
    and a hurried Enter put it back (2026-09-25). An answer on the retired
    network is forgotten before the questions are asked, so the template's
    default -- the documented network -- is what is offered."""

    def calls(self, values):
        text = POSTINST.read_text()
        forget = re.search(r"^forget_retired_network\(\) \{.*?^\}$",
                           text, re.MULTILINE | re.DOTALL)
        ask = re.search(r"^ask_address_questions\(\) \{.*?^\}$",
                        text, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(forget, "postinst defines no forget_retired_network()")
        script = (STUBS + stored(values) + forget.group(0) + "\n"
                  + ask.group(0) + "\nask_address_questions")
        return subprocess.run(["sh", "-c", script], check=True,
                              capture_output=True, text=True).stdout.splitlines()

    def test_an_old_address_is_reset_before_it_is_shown(self):
        calls = self.calls({"address": "192.168.43.57/24",
                            "gateway": "192.168.43.1", "dns": "192.168.43.1"})
        for question in ("address", "gateway", "dns"):
            reset = f"reset a3-core/{question}"
            self.assertIn(reset, calls)
            self.assertLess(calls.index(reset),
                            calls.index(f"input high a3-core/{question}"))

    def test_an_answer_on_the_current_network_stays(self):
        calls = self.calls({"address": "192.168.8.10/24",
                            "gateway": "192.168.8.1", "dns": "192.168.8.1"})
        self.assertFalse([c for c in calls if c.startswith("reset")])

    def test_each_answer_is_judged_on_its_own(self):
        calls = self.calls({"address": "192.168.8.10/24",
                            "gateway": "192.168.43.1", "dns": "192.168.8.1"})
        self.assertEqual(["reset a3-core/gateway"],
                         [c for c in calls if c.startswith("reset")])

    def test_a_network_that_only_looks_alike_stays(self):
        """192.168.4.x and 10.192.168.43.x are not the retired network."""
        calls = self.calls({"address": "192.168.4.30/24",
                            "gateway": "10.192.168.43", "dns": ""})
        self.assertFalse([c for c in calls if c.startswith("reset")])


class NetworkSetupIsOffered(unittest.TestCase):
    def test_whether_to_configure_the_network_is_asked_where_it_is_seen(self):
        """At `medium` a fresh machine never saw the question, answered it
        with its default (no), and never got a network at all."""
        self.assertRegex(POSTINST.read_text(),
                         r"db_input (high|critical) a3-core/configure-network")


if __name__ == "__main__":
    unittest.main()
