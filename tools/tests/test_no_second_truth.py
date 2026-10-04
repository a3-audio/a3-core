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

    def test_an_aux_return_address(self):
        self.assertTrue(self.found_in("desk.py", 'send("/aux-return/stem/push", 1)\n'))

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


class TheDesksBootstrap(unittest.TestCase):
    """Step 2 of truth-from-core: a desk with no truth must know where Core
    speaks -- one port and one word, allowed by name, nowhere else."""

    def test_the_desks_bootstrap_pair_is_allowed_by_name(self):
        for literal in (7790, "/core/here"):
            self.assertIn(("a3-mixer/software/scripts/a3_mixer_truth.py", literal),
                          second_truth.ALLOWED_LITERALS)


class TheCppKeepersBootstrap(unittest.TestCase):
    """Step 3 of truth-from-core: StemDeck's and Motion's keepers know the
    announce port and word before they have a truth -- there and nowhere
    else in those repos."""

    KEEPER = 'constexpr int announcePort = 7790;\ninline const char* announceAddress = "/core/here";\n'

    def found(self, repo, name):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / repo
            path = root / name
            path.parent.mkdir(parents=True)
            path.write_text(self.KEEPER)
            return second_truth.findings({repo: root}, PORTS)

    def test_the_keepers_may_say_it(self):
        self.assertEqual(self.found("stemdeck", "Source/TruthKeeper.h"), [])
        self.assertEqual(self.found("a3-motion-ui", "src/a3-motion-engine/TruthKeeper.hh"), [])

    def test_any_other_file_may_not(self):
        self.assertEqual(len(self.found("stemdeck", "Source/Elsewhere.h")), 2)


class TheSystemSaysNone(unittest.TestCase):
    """Across the checkouts beside this one -- the a3-system workspace. A repo
    that is not there is named, not passed over: a guard that looked at
    nothing would read as a system with nothing to find."""

    def test_the_checkouts_are_the_umbrellas_submodules(self):
        """Since 2026-10-04 ~/a3-system is the umbrella's checkout: the repos
        sit beside a3-core, and a3-motion-ui is a3-motion's ui."""
        repos = second_truth.checkouts(ROOT)
        self.assertEqual(repos["a3-motion-ui"], ROOT.parent / "a3-motion" / "ui")
        self.assertEqual(repos["stemdeck"], ROOT.parent / "stemdeck")

    def test_no_second_truth_anywhere(self):
        repos = second_truth.checkouts(ROOT)
        absent = [name for name, path in repos.items() if not path.is_dir()]
        if absent:
            self.skipTest(f"not beside this checkout: {', '.join(absent)} -- "
                          f"run this in the a3-system workspace")
        found = second_truth.findings(repos, PORTS)
        self.assertEqual([f"{label}:{line}: {value!r}" for label, line, value in found], [])

    def test_no_second_truth_in_this_checkout(self):
        # Runs where the test above skips: in a worktree, Core's own words
        # would otherwise first meet the guard after the merge.
        found = second_truth.findings({"a3-core": ROOT}, PORTS)
        self.assertEqual([f"{label}:{line}: {value!r}" for label, line, value in found], [])


if __name__ == "__main__":
    unittest.main()
