"""No OSC address, port or IP is written anywhere but the one truth.

Every one of them lives in a3-osc.json since 2026-09-30, and every device
reads it from there. tools/second_truth.py looks through the code of every
repo of the system for a literal that says one anyway; this holds it to
finding none. Its mechanics are tested here on made-up repos, so a guard that
has gone blind cannot pass for a clean system.
"""

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

import a3_osc          # noqa: E402
import second_truth    # noqa: E402

TRUTH = a3_osc.load(ROOT / "platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json")
PORTS = second_truth.truth_ports(TRUTH)


class TheGuardSees(unittest.TestCase):
    def found_in(self, name, text):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "a3-mixer"
            path = repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            return second_truth.findings({"a3-mixer": repo}, PORTS)

    def test_an_address_in_python(self):
        self.assertTrue(self.found_in("desk.py", 'send("/channel/1/volume", 0.5)\n'))

    def test_an_fx_return_address(self):
        self.assertTrue(self.found_in("desk.py", 'send("/fx-return/stem/push", 1)\n'))

    def test_an_ip_in_cpp(self):
        self.assertTrue(self.found_in("net.cpp", 'auto core = "192.168.8.10";\n'))

    def test_a_port_in_cpp(self):
        self.assertTrue(self.found_in("net.hh", "int port = 7772;\n"))

    def test_a_port_in_python(self):
        self.assertTrue(self.found_in("desk.py", "PORT = 9000\n"))

    def test_an_address_in_a_unit_file(self):
        # StemDeck's zita-j2n unit sent to Core's old address for days; the
        # guard only read code then.
        self.assertTrue(self.found_in(".config/systemd/user/zita-j2n.service",
                                      "[Service]\nExecStart=/usr/bin/zita-j2n --chan 10 192.168.43.129 65100\n"))

    def test_not_a_units_other_numbers(self):
        self.assertEqual(self.found_in("x.service",
                                       "[Service]\nCPUAffinity=1 2 3\nExecStart=/usr/bin/zita-n2j --chan 1-2 --buff 20\n"), [])

    def test_not_through_a_symlink(self):
        # The package's default.target.wants/ links point at the machine's
        # live ~/.config -- that is the rig, not the repository.
        with tempfile.TemporaryDirectory() as tmp:
            outside = Path(tmp) / "live.service"
            outside.write_text("[Service]\nExecStart=/usr/bin/zita-j2n 192.168.43.129 65100\n")
            repo = Path(tmp) / "a3-core"
            repo.mkdir()
            (repo / "zita-j2n.service").symlink_to(outside)
            self.assertEqual(second_truth.findings({"a3-core": repo}, PORTS), [])

    def test_not_in_a_comment(self):
        self.assertEqual(self.found_in("net.cpp", '// sends "/channel/1/volume" to 9000\n'), [])

    def test_not_in_a_docstring(self):
        self.assertEqual(self.found_in("desk.py", '"""Sends /channel/1/volume."""\n'), [])

    def test_not_in_a_test(self):
        self.assertEqual(self.found_in("tests/test_desk.py", 'A = "/channel/1/volume"\n'), [])

    def test_not_every_interface(self):
        self.assertEqual(self.found_in("desk.py", 'BIND = "0.0.0.0"\n'), [])

    def test_not_reapers_words(self):
        self.assertEqual(self.found_in("core.py", 'A = "/track/3/fx/1/bypass"\n'), [])


class TheSystemSaysNone(unittest.TestCase):
    """Across the checkouts beside this one -- the a3-system workspace. A repo
    that is not there is named, not passed over: a guard that looked at
    nothing would read as a system with nothing to find."""

    def test_no_second_truth_anywhere(self):
        repos = {name: ROOT.parent / name for name in second_truth.REPOS}
        absent = [name for name, path in repos.items() if not path.is_dir()]
        if absent:
            self.skipTest(f"not beside this checkout: {', '.join(absent)} -- "
                          f"run this in the a3-system workspace")
        found = second_truth.findings(repos, PORTS)
        self.assertEqual([f"{label}:{line}: {value!r}" for label, line, value in found], [])


if __name__ == "__main__":
    unittest.main()
