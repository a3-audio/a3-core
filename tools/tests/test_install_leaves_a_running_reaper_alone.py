"""An upgrade does not reinstall what is already there.

The postinst starts a3-user-install.service on every configure, and
user_install.sh used to fetch REAPER, two plugins and rebuild beat-analyzer
each time -- on the live rig. On 2026-09-26 REAPER's installer emptied
~/.local/opt/REAPER under the running REAPER, which crashed in the same second
(a3-core#55). Each component is now installed only when the file it leaves
behind is missing; A3_REINSTALL=1 fetches them again on purpose, and then
REAPER is stopped first and started again afterwards, even if a step fails.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = (Path(__file__).resolve().parents[2]
          / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
          / "share/a3-core/recipes/user_install.sh")


def functions(*names):
    text = SCRIPT.read_text()
    bodies = []
    for name in names:
        body = re.search(rf"^{name}\(\) \{{.*?^\}}$", text, re.MULTILINE | re.DOTALL)
        if body is None:
            raise AssertionError(f"user_install.sh defines no {name}()")
        bodies.append(body.group(0))
    return "\n".join(bodies)


def bash(code, env=None):
    return subprocess.run(["bash", "-c", code], capture_output=True, text=True,
                          env=env)


class Wanted(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.there = Path(self.tmp.name) / "reaper"
        self.there.write_text("")
        self.missing = Path(self.tmp.name) / "nothing"

    def tearDown(self):
        self.tmp.cleanup()

    def run_wanted(self, path, reinstall=None):
        prefix = f"A3_REINSTALL={reinstall} " if reinstall else "unset A3_REINSTALL; "
        return bash(f"{functions('wanted')}\n{prefix}wanted '{path}' REAPER")

    def test_what_is_missing_is_installed(self):
        self.assertEqual(0, self.run_wanted(self.missing).returncode)

    def test_what_is_there_is_left_alone_and_says_so(self):
        done = self.run_wanted(self.there)
        self.assertEqual(1, done.returncode)
        self.assertIn("SKIP", done.stdout)
        self.assertIn("A3_REINSTALL=1", done.stdout)

    def test_on_purpose_it_is_installed_again(self):
        self.assertEqual(0, self.run_wanted(self.there, reinstall=1).returncode)


class EveryDownloadIsGuarded(unittest.TestCase):
    def test_no_fetch_or_build_outside_a_wanted_block(self):
        guard_indent = None
        for number, line in enumerate(SCRIPT.read_text().splitlines(), 1):
            opened = re.match(r"(\s*)if wanted ", line)
            if opened:
                guard_indent = opened.group(1)
            elif guard_indent is not None and line == guard_indent + "fi":
                guard_indent = None
            elif re.search(r"\bwget\b|install-reaper\.sh|\./build\.sh", line) \
                    and not line.lstrip().startswith("#"):
                self.assertIsNotNone(guard_indent,
                                f"line {number} runs unguarded: {line.strip()}")


class ReaperIsPausedOnlyOnPurpose(unittest.TestCase):
    """A fake systemctl on PATH records what the script would ask of systemd."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        bin_dir = Path(self.tmp.name)
        self.log = bin_dir / "calls"
        fake = bin_dir / "systemctl"
        fake.write_text(f'#!/bin/sh\necho "$*" >> {self.log}\n'
                        '[ "$2" = is-active ] && exit "${REAPER_ACTIVE_EXIT:-0}"\n'
                        'exit 0\n')
        fake.chmod(0o755)
        self.path = f"{bin_dir}:/usr/bin:/bin"

    def tearDown(self):
        self.tmp.cleanup()

    def calls(self, code, **env):
        bash(f"{functions('pause_reaper', 'resume_reaper')}\n"
             "reaper_paused=0\ntrap resume_reaper EXIT\n" + code,
             env={"PATH": self.path, **env})
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_an_ordinary_upgrade_never_touches_reaper(self):
        self.assertEqual([], self.calls("pause_reaper"))

    def test_a_reinstall_stops_a_running_reaper_and_starts_it_again(self):
        calls = self.calls("pause_reaper", A3_REINSTALL="1")
        self.assertIn("--user stop a3-reaper.service", calls)
        self.assertEqual("--user start a3-reaper.service", calls[-1])

    def test_it_starts_again_even_when_a_step_fails(self):
        calls = self.calls("set -e\npause_reaper\nfalse", A3_REINSTALL="1")
        self.assertEqual("--user start a3-reaper.service", calls[-1])

    def test_a_stopped_reaper_stays_stopped(self):
        calls = self.calls("pause_reaper", A3_REINSTALL="1", REAPER_ACTIVE_EXIT="3")
        self.assertNotIn("--user stop a3-reaper.service", calls)
        self.assertNotIn("--user start a3-reaper.service", calls)


if __name__ == "__main__":
    unittest.main()
