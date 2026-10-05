"""The package brings no display manager (decided 2026-10-05).

A Core used to depend on lightdm and ship its autologin config. Every screen
role -- Core, StemDeck, Motion -- needs the same thing, a user logged in on X
with i3, so the a3-system installer now sets that up once in its shared base:
getty autologin on tty1, then startx. A display manager in this package would
be a second way to start X fighting the first one for the screen.

xinit and i3 stay in Depends: a Core installed from the bare package, without
the installer, still runs REAPER under i3.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
CONTROL = PACKAGE / "DEBIAN/control"
CONFFILES = PACKAGE / "DEBIAN/conffiles"


def depends():
    line = re.search(r"^Depends: (.*)$", CONTROL.read_text(), re.MULTILINE).group(1)
    return [entry.strip().split()[0] for entry in line.split(",")]


class ThePackageHasNoDisplayManager(unittest.TestCase):
    def test_lightdm_is_not_a_dependency(self):
        self.assertNotIn("lightdm", depends())

    def test_x_and_i3_stay(self):
        self.assertIn("xinit", depends())
        self.assertIn("i3", depends())

    def test_no_lightdm_config_ships(self):
        self.assertFalse((PACKAGE / "etc/lightdm").exists())

    def test_conffiles_name_no_lightdm_file(self):
        self.assertNotIn("lightdm", CONFFILES.read_text())

    def test_nothing_in_the_package_mentions_lightdm(self):
        for path in PACKAGE.rglob("*"):
            if not path.is_file() or path.suffix in (".png", ".jpg", ".pyc"):
                continue
            self.assertNotIn("lightdm", path.read_text(errors="replace").lower(), path)



USER_UNITS = PACKAGE / "home/aaa/.local/share/a3-core/config/systemd/user"
WAIT = "ExecStartPre=/home/aaa/.local/bin/a3-wait-for-the-screen"


class WindowsWaitForTheScreen(unittest.TestCase):
    """Without a display manager, getty logs aaa in before X is up, so the
    user manager starts these units before ~/.xinitrc has imported DISPLAY
    (2026-10-05). Each unit that opens a window names its display and waits
    for the screen itself, as a3-bar-per-workspace already did."""

    def test_each_window_names_the_display_and_waits(self):
        for name in ("a3-reaper.service", "qjackctl.service", "a3-bar-per-workspace.service"):
            text = (USER_UNITS / name).read_text()
            self.assertIn("Environment=DISPLAY=:0", text, name)
            self.assertIn(WAIT, text, name)


if __name__ == "__main__":
    unittest.main()
