"""x11vnc asks for a password, and will not run without one (spec
devices-and-remote-access, 2026-10-05).

The rig's screen goes out through the Vigor to the home network once the
VNC forward opens, and x11vnc ran with no password at all: anyone who could
reach the port had the panel. Now it reads ~/.vnc/passwd, which the
maintainer creates on the machine (`x11vnc -storepasswd`) and which no repo
and no package ever carries -- a shipped password is everybody's password.
Without the file the unit fails with the path in `systemctl status`, rather
than running open.
"""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
UNIT = PACKAGE / "etc/systemd/system/x11vnc.service"
PASSWORD = "/home/aaa/.vnc/passwd"


def unit_lines():
    return [line.strip() for line in UNIT.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def exec_start():
    return next(line for line in unit_lines() if line.startswith("ExecStart="))


class X11vncAsksForAPassword(unittest.TestCase):
    def test_it_reads_the_password_file(self):
        self.assertIn(f"-rfbauth {PASSWORD}", exec_start())

    def test_it_never_runs_open(self):
        for open_door in ("-nopw", "-passwd ", "-usepw"):
            with self.subTest(option=open_door):
                self.assertNotIn(open_door, exec_start())

    def test_it_does_not_start_without_the_file(self):
        """An assert, not a condition: an unmet condition leaves the unit
        quietly inactive, a failed assert marks it failed and names the path.
        And not an ExecStartPre test, which Restart=on-failure would retry."""
        self.assertIn(f"AssertFileNotEmpty={PASSWORD}", unit_lines())

    def test_the_unit_says_how_to_make_the_file(self):
        self.assertIn("x11vnc -storepasswd", UNIT.read_text())


class NoPasswordIsShipped(unittest.TestCase):
    def test_the_package_tree_has_no_vnc_directory(self):
        self.assertEqual([], [str(p) for p in PACKAGE.rglob(".vnc")])

    def test_git_would_not_take_one(self):
        ignored = subprocess.run(
            ["git", "check-ignore", "-q",
             str(PACKAGE / "home/aaa/.vnc/passwd")],
            cwd=ROOT, check=False)
        self.assertEqual(0, ignored.returncode)
